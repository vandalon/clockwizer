#pragma once
#include <WiFiServer.h>
#include <queue>
#include <string>
#include <TelnetStream.h>

class NotificationServer {
private:
    WiFiServer server;
    std::queue<std::pair<String, unsigned long>> notifications;  // pair of message and duration
    static const int MAX_NOTIFICATIONS = 5;
    static const int TCP_PORT = 8266;

public:
    NotificationServer() : server(TCP_PORT) {}
    
    void begin() {
        server.begin();
        TelnetStream.println("[Notification] Server started on port 8266");
    }

    void handle() {
        if (WiFi.status() != WL_CONNECTED) return;
        
        WiFiClient client = server.available();
        if (client) {
            TelnetStream.println("[Notification] New client connected");
            
            unsigned long timeout = millis();
            while (client.connected() && millis() - timeout < 1000) {
                if (client.available()) {
                    String input = client.readStringUntil('\n');
                    input.trim();
                    
                    if (input.length() > 0) {
                        // Parse duration and message
                        int spaceIndex = input.indexOf(' ');
                        if (spaceIndex > 0) {
                            String durationStr = input.substring(0, spaceIndex);
                            String message = input.substring(spaceIndex + 1);
                            message.toUpperCase();
                            unsigned long duration = durationStr.toInt() * 1000; // Convert to milliseconds
                            
                            if (duration > 0 && message.length() > 0) {
                                if (notifications.size() >= MAX_NOTIFICATIONS) {
                                    notifications.pop();
                                }
                                notifications.push(std::make_pair(message, duration));
                                client.println("OK");
                                TelnetStream.printf("[Notification] Received: %s (duration: %lu seconds)\n", 
                                    message.c_str(), duration/1000);
                            } else {
                                client.println("ERROR: Invalid format. Use: <seconds> <message>");
                            }
                        } else {
                            client.println("ERROR: Invalid format. Use: <seconds> <message>");
                        }
                    }
                }
                delay(1);
            }
            client.stop();
            TelnetStream.println("[Notification] Client disconnected");
        }
    }

    bool hasNotification() {
        return !notifications.empty();
    }

    std::pair<String, unsigned long> getNextNotification() {
        if (notifications.empty()) return std::make_pair(String(""), 0UL);
        auto notification = notifications.front();
        notifications.pop();
        return notification;
    }
};