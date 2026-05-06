#include "wf/json.hpp"
#include "wf/workflow_json.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{

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

void printUsage(const char* prog)
{
    std::cout << "usage: " << prog << " <command> [args]\n";
    std::cout << "\n";
    std::cout << "commands:\n";
    std::cout << "  validate <file>  validate a workflow definition JSON file\n";
    std::cout << "  parse <file>     parse and display a workflow definition\n";
}

} // namespace

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

    std::cerr << "error: unknown command: " << command << "\n\n";
    printUsage(argv[0]);
    return 1;
}
