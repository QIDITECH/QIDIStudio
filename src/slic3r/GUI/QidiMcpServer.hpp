#ifndef slic3r_GUI_QidiMcpServer_hpp_
#define slic3r_GUI_QidiMcpServer_hpp_

#include <memory>

namespace Slic3r::GUI {

class QDSDeviceManager;

// Loopback-only MCP endpoint for QIDI Studio project, preset, and slicing APIs.
// Print start is local/LAN only and requires a short-lived, single-use confirmation token.
class QidiMcpServer
{
public:
    explicit QidiMcpServer(QDSDeviceManager* device_manager);
    ~QidiMcpServer();

    QidiMcpServer(const QidiMcpServer&) = delete;
    QidiMcpServer& operator=(const QidiMcpServer&) = delete;

    bool start();
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Slic3r::GUI

#endif
