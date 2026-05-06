#pragma once

#include "wf/workflow_service.hpp"

#include <memory>

namespace workflow::http
{

class WorkflowHttpServer
{
  public:
    WorkflowHttpServer(
        WorkflowService& service,
        int port
    );
    ~WorkflowHttpServer();

    int bind();
    void start();
    void stop();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace workflow::http
