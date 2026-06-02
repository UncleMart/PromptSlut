#include <QApplication>
#include "qt_ui.h"
#include "worker.h"
#include "tool_router.h"
#include "tools.h"
#include "logger.h"
#include <iostream>

int main(int argc, char *argv[]) {
    // 1. Initialize the background worker
    // In the original code, Worker is created and passed to the UI
    Worker worker;
    
    // Register default tools (following the original main.cpp logic)
    worker.register_tool(std::make_shared<FileTool>());
    worker.register_tool(std::make_shared<ShellTool>());
    worker.register_tool(std::make_shared<DiskSearchTool>());
    worker.register_tool(std::make_shared<SearchTool>());
    worker.register_tool(std::make_shared<FetchTool>());
    worker.register_tool(std::make_shared<ClockTool>());
    worker.register_tool(std::make_shared<ClipboardTool>());
    worker.register_tool(std::make_shared<GrepTool>());
    worker.register_tool(std::make_shared<EditTool>());

    ToolRegistry temp_reg;
    temp_reg.register_tool(std::make_shared<FileTool>());
    temp_reg.register_tool(std::make_shared<ShellTool>());
    temp_reg.register_tool(std::make_shared<DiskSearchTool>());
    temp_reg.register_tool(std::make_shared<SearchTool>());
    temp_reg.register_tool(std::make_shared<FetchTool>());
    temp_reg.register_tool(std::make_shared<ClockTool>());
    temp_reg.register_tool(std::make_shared<ClipboardTool>());
    temp_reg.register_tool(std::make_shared<GrepTool>());
    temp_reg.register_tool(std::make_shared<EditTool>());
    worker.set_tool_registry(temp_reg.to_json());
    
    // 2. Launch the Qt Application
    QApplication app(argc, argv);
    
    QtUiApp gui(&worker);
    gui.show();
    
    return app.exec();
}
