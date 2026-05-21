#pragma once

#include "NetworkCommon.hpp"

#include <SFML/Network.hpp>
#include <SFML/System/Vector2.hpp>

#include <string>
#include <vector>

class NetworkServer {
public:
    bool start(unsigned short port);
    void stop();
    void update();
    bool isConnected() const;
    bool hasPendingMove() const;
    NetMove popMove();
    bool sendMove(int row, int col);
    bool sendRestart();
    bool sendUndo();
    bool sendSurrender();
    bool sendRoomConfig(int mode, int undoCount, int turnTime, int selectedMapIndex, int obstacleDynamic);
    bool sendObstacleSync(const std::vector<sf::Vector2i>& positions);
    bool sendCardEvent(int card0, int card1, int card2);
    bool sendCardSelected(int index);
    bool sendWheelResult(bool hostPicks);
    bool sendWheelStartAngle(int startAngle);
    bool sendCardEffectSeed(std::uint32_t seed);
    std::string localAddress() const;

private:
    sf::TcpListener listener_;
    sf::TcpSocket client_;
    bool clientConnected_ = false;
    std::vector<NetMove> pendingMoves_;
    std::vector<std::uint8_t> recvBuffer_;
};
