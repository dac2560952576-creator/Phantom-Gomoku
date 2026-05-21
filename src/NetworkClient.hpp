#pragma once

#include "NetworkCommon.hpp"

#include <SFML/Network.hpp>
#include <SFML/System/Vector2.hpp>

#include <string>
#include <vector>

class NetworkClient {
public:
    void startConnecting(const std::string& ip, unsigned short port);
    void disconnect();
    void update();
    bool isConnecting() const;
    bool isConnected() const;
    bool isConnectionFailed() const;
    bool hasPendingMove() const;
    NetMove popMove();
    bool sendMove(int row, int col);
    bool sendRestart();
    bool sendUndo();
    bool sendSurrender();
    bool sendCardSelected(int index);
    bool hasRoomConfig() const;
    bool hasPendingObstacleSync() const;
    std::vector<sf::Vector2i> popObstacleSync();
    RoomConfig roomConfig() const;

private:
    sf::TcpSocket socket_;
    bool connecting_ = false;
    bool connected_ = false;
    bool connectionFailed_ = false;
    sf::IpAddress serverAddress_{0u};
    unsigned short serverPort_ = 0;
    std::vector<NetMove> pendingMoves_;
    std::vector<std::uint8_t> recvBuffer_;
    RoomConfig receivedConfig_;
    std::vector<std::vector<sf::Vector2i>> pendingObstacleSyncs_;
    bool connectAttempted_ = false;
};
