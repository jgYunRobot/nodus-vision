#include <gtest/gtest.h>

#include <string>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>

#include "provider_http_server.hpp"

namespace nodus_vision {
namespace {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

http::response<http::string_body> getResponse(int port, const std::string& target)
{
    asio::io_context context;
    tcp::socket socket(context);
    socket.connect({asio::ip::make_address("127.0.0.1"), static_cast<unsigned short>(port)});
    http::request<http::empty_body> request{http::verb::get, target, 11};
    request.set(http::field::host, "localhost");
    http::write(socket, request);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(socket, buffer, response);
    return response;
}
} // namespace

TEST(ProviderHttpServer, ServesHealthMetadataAndNoStoreResponses)
{
    ProviderHttpServer server("127.0.0.1", 0, 2, 1000, 8192, 4096, {
        []() { return ProviderHttpResponse{200, "application/json", "{\"schema_version\":1,\"state\":\"degraded\"}"}; },
        []() { return ProviderHttpResponse{200, "application/json", "{\"schema_version\":1,\"endpoints\":[\"/health\",\"/metadata\"]}"}; },
        []() { return ProviderHttpResponse{503, "application/json", "{}"}; },
        []() { return ProviderHttpResponse{503, "application/json", "{}"}; },
        []() { return ProviderHttpResponse{503, "application/json", "{}"}; },
        [](const std::string&) { return ProviderHttpResponse{400, "application/json", "{}"}; },
        [](const std::string&) { return ProviderHttpResponse{400, "application/json", "{}"}; },
    });
    server.startServer();
    const http::response<http::string_body> health = getResponse(server.getBoundPort(), "/health");
    EXPECT_EQ(health.result(), http::status::ok);
    EXPECT_EQ(health[http::field::cache_control], "no-store");
    EXPECT_NE(health.body().find("degraded"), std::string::npos);
    const http::response<http::string_body> metadata = getResponse(server.getBoundPort(), "/metadata");
    EXPECT_EQ(metadata.result(), http::status::ok);
    EXPECT_NE(metadata.body().find("metadata"), std::string::npos);
    const http::response<http::string_body> snapshot = getResponse(server.getBoundPort(), "/snapshot/color");
    EXPECT_EQ(snapshot.result(), http::status::service_unavailable);
    EXPECT_EQ(snapshot[http::field::cache_control], "no-store");
    server.stopServer();
}

} // namespace nodus_vision
