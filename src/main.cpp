#include "WebSocketData.h"
#include <iostream>
#include <ostream>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <uWebSockets/src/App.h>

// Ogni Client avrà sta roba
struct PerSocketData {
  int user_id;
  std::string username;
};

namespace fs = std::filesystem;
using AppWebSocket = uWS::WebSocket<true, true, PerSocketData>;
using AppRequest = uWS::HttpRequest;
using AppResponse = uWS::HttpResponse<true>;

std::string readFile(std::string_view path) {
    std::ifstream is(path.data(), std::ios::binary);
    if (!is) return "";
    std::stringstream buffer;
    buffer << is.rdbuf();
    return buffer.str();
}

std::string getMimeType(std::string path) {
    if (path.ends_with(".html")) return "text/html";
    if (path.ends_with(".js"))   return "text/javascript";
    if (path.ends_with(".css"))  return "text/css";
    if (path.ends_with(".svg"))  return "image/svg+xml";
    if (path.ends_with(".png"))  return "image/png";
    return "application/octet-stream";
}
using json = nlohmann::json;

int main() {
  uWS::SocketContextOptions ssl_options;
  ssl_options.key_file_name = "key.pem";
  ssl_options.cert_file_name = "cert.pem";

  const int PORT = 9999;

  uWS::SSLApp app = uWS::SSLApp(ssl_options)
    .post("/create-topic", [](AppResponse *response, AppRequest *request) {
      response->onAborted([]() {});
      std::string buffer;

      std::cout << "[POST]: /create-topic RECEIVED" << std::endl;
      response->onData([response, buffer = std::move(buffer)](std::string_view chunk, bool isLast) mutable {
        buffer.append(chunk.data(), chunk.length());
  
        if (isLast) {
          try {
            auto data = nlohmann::json::parse(buffer);
          
            std::string topic = data.value("topic", "default");
            std::cout << "[POST]: /create-topic DATA:" << topic << std::endl;
    
            response->writeHeader("Content-Type", "application/json")
                    ->end("{\"status\": \"OK\", \"topic\": \"" + topic + "\"}");
    
          } catch (const std::exception &e) {
            response->writeStatus("400 Bad Request")->end("Invalid JSON");
          }
        }
      });
    })
    .get("/*", [](AppResponse *response, AppRequest *request) {
      response->onAborted([]() { /* Gestione cleanup */ });

      std::string url = std::string(request->getUrl());
      std::string relativePath = (url == "/") ? "index.html" : url.substr(1);

      fs::path baseDir = fs::current_path() / "public";
      fs::path filePath = baseDir / relativePath;


      if (std::filesystem::exists(filePath) && !std::filesystem::is_directory(filePath)) {
        std::string pathStr = filePath.string();
        std::string_view mime = getMimeType(pathStr);

        response->writeHeader("Content-Type", std::string(mime))
           ->writeHeader("X-Content-Type-Options", "nosniff")
           ->end(readFile(pathStr));
      } else {
        std::cout << "Tried finding: " << filePath << " but 404!" << std::endl;
        // Fallback su index.html per gestire il routing lato client (Svelte)
        response->writeStatus("404 Not Found")->end("File non trovato");
      }
    })
    .ws<PerSocketData>("/*", {
    .open = [](AppWebSocket *ws) {
      std::cout << "Nuovo Socket Aperto!" << std::endl;
    },
    .message = [](AppWebSocket *ws, std::string_view message, uWS::OpCode opCode) {
      try {
        auto j = json::parse(message);
        std::string action = j["action"];
  
        if (action == "join") {
          std::string topic = j["topic"];
          ws->subscribe(topic); // Il cuore del sistema!
          std::cout << "Client iscritto al topic: " << topic << std::endl;
        } 
        else if (action == "broadcast") {
          std::string topic = j["topic"];
          std::string msg = j["msg"];
          // Invia a tutti gli iscritti a quel topic, TRANNE chi invia
          ws->publish(topic, message, opCode, false);
        }
      } catch (...) {}
    }});

    app.listen(9999, [](auto *listen_socket) {
      if (listen_socket) {
        std::cout << "Server HTTPS/WSS in ascolto su https://localhost:9999" << std::endl;
      }
    })
  .run();
  return 0;
}
