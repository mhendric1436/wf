#include "wf/backend/sqlite/sqlite_database.hpp"
#include "wf/backend/sqlite/sqlite_workflow_definition_store.hpp"
#include "wf/backend/sqlite/sqlite_workflow_execution_store.hpp"
#include "wf/backend/sqlite/sqlite_workflow_step_execution_store.hpp"
#include "wf/http/workflow_http_server.hpp"
#include "wf/json.hpp"
#include "wf/logic/step_output_routing_logic.hpp"
#include "wf/transport/http_transport.hpp"
#include "wf/workflow_client.hpp"
#include "wf/workflow_json.hpp"
#include "wf/workflow_orchestrator.hpp"
#include "wf/workflow_service.hpp"

#include <csignal>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{

// -----------------------------------------------------------------------------
// Utilities
// -----------------------------------------------------------------------------

std::string readFile(const std::string& path)
{
    std::ifstream file(path);

    if (!file.is_open())
    {
        throw std::runtime_error("cannot open file: " + path);
    }

    std::ostringstream buf;
    buf << file.rdbuf();
    return buf.str();
}

// Parses "http://host:port", "host:port", or "host" into {host, port}.
// Default port is 8080.
std::pair<
    std::string,
    int>
parseServerUrl(const std::string& url)
{
    std::string hostPort = url;

    if (hostPort.substr(0, 7) == "http://")
    {
        hostPort = hostPort.substr(7);
    }

    const auto slashPos = hostPort.find('/');
    if (slashPos != std::string::npos)
    {
        hostPort = hostPort.substr(0, slashPos);
    }

    const auto colonPos = hostPort.rfind(':');
    if (colonPos != std::string::npos)
    {
        const std::string host = hostPort.substr(0, colonPos);
        const int port = std::stoi(hostPort.substr(colonPos + 1));
        return {host, port};
    }

    return {hostPort, 8080};
}

workflow::WorkflowClient makeClient(const std::string& server)
{
    const auto [host, port] = parseServerUrl(server);
    return workflow::WorkflowClient(
        std::make_unique<workflow::transport::HttpTransport>(host, port)
    );
}

void printExecution(const workflow::WorkflowExecution& exec)
{
    std::cout << "id:      " << exec.workflowExecutionId << "\n";
    std::cout << "name:    " << exec.workflowName << "\n";
    std::cout << "version: " << exec.workflowVersion << "\n";
    std::cout << "status:  " << workflow::toString(exec.status) << "\n";
    std::cout << "step:    " << exec.currentStepName << "\n";

    if (exec.failureReason.has_value())
    {
        std::cout << "reason:  " << exec.failureReason.value() << "\n";
    }
}

// -----------------------------------------------------------------------------
// Local commands (no server)
// -----------------------------------------------------------------------------

int cmdValidate(const std::string& path)
{
    std::string text;

    try
    {
        text = readFile(path);
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    workflow::json::Value json;

    try
    {
        json = workflow::json::parse(text);
    }
    catch (const workflow::json::JsonParseError& e)
    {
        std::cerr << "invalid: JSON parse error: " << e.what() << "\n";
        return 1;
    }

    const auto result = workflow::validateWorkflowJson(json);

    if (result.valid)
    {
        std::cout << "valid\n";
        return 0;
    }

    std::cerr << "invalid:\n";

    for (const auto& error : result.errors)
    {
        std::cerr << "  - " << error << "\n";
    }

    return 1;
}

int cmdParse(const std::string& path)
{
    std::string text;

    try
    {
        text = readFile(path);
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    workflow::WorkflowDefinition def;

    try
    {
        def = workflow::parseWorkflowDefinitionText(text);
    }
    catch (const std::invalid_argument& e)
    {
        std::cerr << e.what() << "\n";
        return 1;
    }

    std::cout << "name:    " << def.workflowName << "\n";
    std::cout << "version: " << def.workflowVersion << "\n";
    std::cout << "time:    " << def.expectedExecutionTime << "\n";
    std::cout << "start:   " << def.startWorkflowStepName << "\n";
    std::cout << "steps:\n";

    for (const auto& step : def.steps)
    {
        std::cout << "  " << step.name;

        if (step.expectedExecutionTime.has_value())
        {
            std::cout << "  time=" << step.expectedExecutionTime.value();
        }

        if (step.maxRetries.has_value())
        {
            std::cout << "  maxRetries=" << step.maxRetries.value();
        }

        std::cout << "\n";
    }

    return 0;
}

// -----------------------------------------------------------------------------
// Server commands
// -----------------------------------------------------------------------------

// wf register [--server <url>] <file>
int cmdRegister(
    int argc,
    char* argv[]
)
{
    std::string server = "localhost:8080";
    std::string file;

    for (int i = 0; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "--server" && i + 1 < argc)
        {
            server = argv[++i];
        }
        else if (arg[0] != '-')
        {
            file = arg;
        }
    }

    if (file.empty())
    {
        std::cerr << "error: register requires a file argument\n";
        return 1;
    }

    std::string text;

    try
    {
        text = readFile(file);
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    try
    {
        auto client = makeClient(server);
        const auto response = client.registerWorkflowDefinition(
            workflow::RegisterWorkflowDefinitionRequest{
                .definitionJson = workflow::json::parse(text)
            }
        );

        const auto& def = response.definition;
        std::cout << "registered: " << def.workflowName << " v" << def.workflowVersion << "\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

// wf list [--server <url>]
int cmdList(
    int argc,
    char* argv[]
)
{
    std::string server = "localhost:8080";

    for (int i = 0; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "--server" && i + 1 < argc)
        {
            server = argv[++i];
        }
    }

    try
    {
        auto client = makeClient(server);
        const auto response =
            client.listWorkflowDefinitions(workflow::ListWorkflowDefinitionsRequest{});

        if (response.definitions.empty())
        {
            std::cout << "no workflow definitions registered\n";
            return 0;
        }

        for (const auto& key : response.definitions)
        {
            std::cout << key.workflowName << " v" << key.workflowVersion << "\n";
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

// wf start [--server <url>] [--input <json>] <name> <version>
int cmdStart(
    int argc,
    char* argv[]
)
{
    std::string server = "localhost:8080";
    std::string inputJson = "{}";
    std::string name;
    int version = 0;
    std::vector<std::string> positional;

    for (int i = 0; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "--server" && i + 1 < argc)
        {
            server = argv[++i];
        }
        else if (arg == "--input" && i + 1 < argc)
        {
            inputJson = argv[++i];
        }
        else if (arg[0] != '-')
        {
            positional.push_back(arg);
        }
    }

    if (positional.size() < 2)
    {
        std::cerr << "error: start requires <name> and <version> arguments\n";
        return 1;
    }

    name = positional[0];

    try
    {
        version = std::stoi(positional[1]);
    }
    catch (const std::exception&)
    {
        std::cerr << "error: invalid version: " << positional[1] << "\n";
        return 1;
    }

    try
    {
        auto client = makeClient(server);

        workflow::StartWorkflowExecutionRequest request;
        request.workflowName = name;
        request.workflowVersion = version;
        request.input = workflow::json::parse(inputJson);

        const auto response = client.startWorkflowExecution(request);
        printExecution(response.execution);
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

// wf get [--server <url>] <id>
int cmdGet(
    int argc,
    char* argv[]
)
{
    std::string server = "localhost:8080";
    std::string id;

    for (int i = 0; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "--server" && i + 1 < argc)
        {
            server = argv[++i];
        }
        else if (arg[0] != '-')
        {
            id = arg;
        }
    }

    if (id.empty())
    {
        std::cerr << "error: get requires an execution id argument\n";
        return 1;
    }

    try
    {
        auto client = makeClient(server);
        const auto response = client.getWorkflowExecution(
            workflow::GetWorkflowExecutionRequest{.workflowExecutionId = id}
        );

        if (!response.execution.has_value())
        {
            std::cerr << "error: workflow execution not found: " << id << "\n";
            return 1;
        }

        printExecution(response.execution.value());
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

// wf cancel [--server <url>] <id>
int cmdCancel(
    int argc,
    char* argv[]
)
{
    std::string server = "localhost:8080";
    std::string id;

    for (int i = 0; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "--server" && i + 1 < argc)
        {
            server = argv[++i];
        }
        else if (arg[0] != '-')
        {
            id = arg;
        }
    }

    if (id.empty())
    {
        std::cerr << "error: cancel requires an execution id argument\n";
        return 1;
    }

    try
    {
        auto client = makeClient(server);
        const auto response =
            client.cancelWorkflow(workflow::CancelWorkflowRequest{.workflowExecutionId = id});
        printExecution(response.execution);
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

// -----------------------------------------------------------------------------
// wf serve
// -----------------------------------------------------------------------------

workflow::http::WorkflowHttpServer* g_server = nullptr;

void handleSignal(int)
{
    if (g_server != nullptr)
    {
        g_server->stop();
    }
}

int cmdServe(
    int argc,
    char* argv[]
)
{
    int port = 8080;
    std::string dbPath;

    for (int i = 0; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "--port" && i + 1 < argc)
        {
            try
            {
                port = std::stoi(argv[++i]);
            }
            catch (const std::exception&)
            {
                std::cerr << "error: invalid port: " << argv[i] << "\n";
                return 1;
            }
        }
        else if (arg == "--db" && i + 1 < argc)
        {
            dbPath = argv[++i];
        }
    }

    if (dbPath.empty())
    {
        std::cerr << "error: --db <path> is required\n";
        return 1;
    }

    try
    {
        workflow::backend::sqlite::SQLiteDatabase db(dbPath);
        workflow::backend::sqlite::SQLiteWorkflowDefinitionStore definitionStore(db);
        workflow::backend::sqlite::SQLiteWorkflowExecutionStore executionStore(db);
        workflow::backend::sqlite::SQLiteWorkflowStepExecutionStore stepExecutionStore(db);

        workflow::logic::StepOutputRoutingLogic logic;
        workflow::WorkflowOrchestrator orchestrator(
            definitionStore, executionStore, stepExecutionStore, logic
        );
        workflow::WorkflowService service(orchestrator);
        workflow::http::WorkflowHttpServer server(service, port);

        g_server = &server;
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);

        const int actualPort = server.bind();
        std::cout << "listening on port " << actualPort << "\n";
        server.start();

        g_server = nullptr;
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

// -----------------------------------------------------------------------------
// Usage
// -----------------------------------------------------------------------------

void printUsage(const char* prog)
{
    std::cout << "usage: " << prog << " <command> [args]\n";
    std::cout << "\n";
    std::cout << "local commands:\n";
    std::cout << "  validate <file>                    validate a workflow definition JSON file\n";
    std::cout << "  parse <file>                       parse and display a workflow definition\n";
    std::cout << "\n";
    std::cout << "server commands (--server defaults to localhost:8080):\n";
    std::cout << "  serve [--port <n>] --db <path>     start the HTTP server\n";
    std::cout << "  register [--server <url>] <file>   register a workflow definition\n";
    std::cout << "  list [--server <url>]              list registered workflow definitions\n";
    std::cout << "  start [--server <url>] [--input <json>] <name> <version>  start an execution\n";
    std::cout << "  get [--server <url>] <id>          get a workflow execution\n";
    std::cout << "  cancel [--server <url>] <id>       cancel a workflow execution\n";
}

} // namespace

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main(
    int argc,
    char* argv[]
)
{
    if (argc < 2)
    {
        printUsage(argv[0]);
        return 1;
    }

    const std::string command = argv[1];

    if (command == "validate")
    {
        if (argc < 3)
        {
            std::cerr << "error: validate requires a file argument\n";
            return 1;
        }
        return cmdValidate(argv[2]);
    }

    if (command == "parse")
    {
        if (argc < 3)
        {
            std::cerr << "error: parse requires a file argument\n";
            return 1;
        }
        return cmdParse(argv[2]);
    }

    if (command == "serve")
        return cmdServe(argc - 2, argv + 2);

    if (command == "register")
        return cmdRegister(argc - 2, argv + 2);

    if (command == "list")
        return cmdList(argc - 2, argv + 2);

    if (command == "start")
        return cmdStart(argc - 2, argv + 2);

    if (command == "get")
        return cmdGet(argc - 2, argv + 2);

    if (command == "cancel")
        return cmdCancel(argc - 2, argv + 2);

    std::cerr << "error: unknown command: " << command << "\n\n";
    printUsage(argv[0]);
    return 1;
}
