#include "NetworkClient.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>

void NetworkClient::startConnecting(const std::string& ip, unsigned short port) {
    disconnect();

    std::array<int, 4> octets{};
    char dot;
    std::istringstream stream(ip);
    stream >> octets[0] >> dot >> octets[1] >> dot >> octets[2] >> dot >> octets[3];

    serverAddress_ = sf::IpAddress(
        static_cast<std::uint8_t>(octets[0]),
        static_cast<std::uint8_t>(octets[1]),
        static_cast<std::uint8_t>(octets[2]),
        static_cast<std::uint8_t>(octets[3])
    );
    serverPort_ = port;
    connecting_ = true;
    connectAttempted_ = false;
}

void NetworkClient::disconnect() {
    socket_.disconnect();
    connecting_ = false;
    connected_ = false;
    connectionFailed_ = false;
    connectAttempted_ = false;
    pendingMoves_.clear();
    recvBuffer_.clear();
    receivedConfig_ = RoomConfig{};
    pendingObstacleSyncs_.clear();
}

void NetworkClient::update() {
    if (!connecting_ && !connected_) {
        return;
    }

    if (connecting_ && !connected_) {
        if (!connectAttempted_) {
            connectAttempted_ = true;
            return;
        }
        socket_.setBlocking(true);
        if (socket_.connect(serverAddress_, serverPort_, sf::seconds(3)) == sf::Socket::Status::Done) {
            socket_.setBlocking(false);
            connected_ = true;
            connecting_ = false;
        } else {
            connecting_ = false;
            connectionFailed_ = true;
        }
        return;
    }

    std::array<std::uint8_t, 128> chunk{};
    std::size_t received = 0;
    sf::Socket::Status recvStatus;
    while ((recvStatus = socket_.receive(chunk.data(), chunk.size(), received)) == sf::Socket::Status::Done) {
        for (std::size_t i = 0; i < received; ++i) {
            recvBuffer_.push_back(chunk[i]);
        }
    }
    if (recvStatus == sf::Socket::Status::Disconnected || recvStatus == sf::Socket::Status::Error) {
        socket_.disconnect();
        connected_ = false;
    }

    while (recvBuffer_.size() >= 3 && recvBuffer_[0] == 0x01) {
        NetMove move;
        move.row = static_cast<int>(recvBuffer_[1]);
        move.col = static_cast<int>(recvBuffer_[2]);
        pendingMoves_.push_back(move);
        recvBuffer_.erase(recvBuffer_.begin(), recvBuffer_.begin() + 3);
    }

    if (recvBuffer_.size() >= 1 && recvBuffer_[0] == 0x02) {
        NetMove restartSignal;
        restartSignal.row = -1;
        restartSignal.col = -1;
        pendingMoves_.push_back(restartSignal);
        recvBuffer_.erase(recvBuffer_.begin());
    }

    if (recvBuffer_.size() >= 1 && recvBuffer_[0] == 0x03) {
        NetMove undoSignal;
        undoSignal.row = -2;
        undoSignal.col = -2;
        pendingMoves_.push_back(undoSignal);
        recvBuffer_.erase(recvBuffer_.begin());
    }

    if (recvBuffer_.size() >= 1 && recvBuffer_[0] == 0x04) {
        NetMove surrenderSignal;
        surrenderSignal.row = -3;
        surrenderSignal.col = -3;
        pendingMoves_.push_back(surrenderSignal);
        recvBuffer_.erase(recvBuffer_.begin());
    }

    if (recvBuffer_.size() >= 6 && recvBuffer_[0] == 0x05) {
        receivedConfig_.mode = static_cast<int>(recvBuffer_[1]);
        receivedConfig_.undoCount = static_cast<int>(recvBuffer_[2]);
        receivedConfig_.turnTime = static_cast<int>(recvBuffer_[3]);
        receivedConfig_.selectedMapIndex = static_cast<int>(recvBuffer_[4]);
        receivedConfig_.obstacleDynamic = static_cast<int>(recvBuffer_[5]);
        receivedConfig_.received = true;
        recvBuffer_.erase(recvBuffer_.begin(), recvBuffer_.begin() + 6);
    }

    if (recvBuffer_.size() >= 2 && recvBuffer_[0] == 0x06) {
        const int count = static_cast<int>(recvBuffer_[1]);
        const std::size_t needed = 2 + static_cast<std::size_t>(count) * 2;
        if (recvBuffer_.size() >= needed) {
            std::vector<sf::Vector2i> positions;
            for (int i = 0; i < count; ++i) {
                positions.push_back({static_cast<int>(recvBuffer_[2 + i * 2]),
                                     static_cast<int>(recvBuffer_[2 + i * 2 + 1])});
            }
            pendingObstacleSyncs_.push_back(std::move(positions));
            recvBuffer_.erase(recvBuffer_.begin(), recvBuffer_.begin() + static_cast<std::ptrdiff_t>(needed));
        }
    }

    if (recvBuffer_.size() >= 4 && recvBuffer_[0] == 0x07) {
        NetMove cardEvent;
        cardEvent.row = -4;
        cardEvent.col = (static_cast<int>(recvBuffer_[1]) << 16) |
                        (static_cast<int>(recvBuffer_[2]) << 8) |
                        static_cast<int>(recvBuffer_[3]);
        pendingMoves_.push_back(cardEvent);
        recvBuffer_.erase(recvBuffer_.begin(), recvBuffer_.begin() + 4);
    }

    if (recvBuffer_.size() >= 2 && recvBuffer_[0] == 0x08) {
        NetMove cardSelect;
        cardSelect.row = -5;
        cardSelect.col = static_cast<int>(recvBuffer_[1]);
        pendingMoves_.push_back(cardSelect);
        recvBuffer_.erase(recvBuffer_.begin(), recvBuffer_.begin() + 2);
    }

    if (recvBuffer_.size() >= 2 && recvBuffer_[0] == 0x0A) {
        NetMove wheelResult;
        wheelResult.row = -7;
        wheelResult.col = static_cast<int>(recvBuffer_[1]);
        pendingMoves_.push_back(wheelResult);
        recvBuffer_.erase(recvBuffer_.begin(), recvBuffer_.begin() + 2);
    }

    if (recvBuffer_.size() >= 3 && recvBuffer_[0] == 0x0B) {
        NetMove wheelAngleSync;
        wheelAngleSync.row = -8;
        wheelAngleSync.col = static_cast<int>(recvBuffer_[1]) | (static_cast<int>(recvBuffer_[2]) << 8);
        pendingMoves_.push_back(wheelAngleSync);
        recvBuffer_.erase(recvBuffer_.begin(), recvBuffer_.begin() + 3);
    }

    if (recvBuffer_.size() >= 5 && recvBuffer_[0] == 0x0C) {
        NetMove seedSync;
        seedSync.row = -9;
        seedSync.col = static_cast<int>(recvBuffer_[1]) |
                       (static_cast<int>(recvBuffer_[2]) << 8) |
                       (static_cast<int>(recvBuffer_[3]) << 16) |
                       (static_cast<int>(recvBuffer_[4]) << 24);
        pendingMoves_.push_back(seedSync);
        recvBuffer_.erase(recvBuffer_.begin(), recvBuffer_.begin() + 5);
    }
}

bool NetworkClient::isConnecting() const {
    return connecting_;
}

bool NetworkClient::isConnected() const {
    return connected_;
}

bool NetworkClient::isConnectionFailed() const {
    return connectionFailed_;
}

bool NetworkClient::hasPendingMove() const {
    return !pendingMoves_.empty();
}

NetMove NetworkClient::popMove() {
    auto move = pendingMoves_.front();
    pendingMoves_.erase(pendingMoves_.begin());
    return move;
}

bool NetworkClient::sendMove(int row, int col) {
    if (!connected_) {
        return false;
    }
    const std::uint8_t packet[] = {0x01, static_cast<std::uint8_t>(row), static_cast<std::uint8_t>(col)};
    return socket_.send(packet, sizeof(packet)) == sf::Socket::Status::Done;
}

bool NetworkClient::sendRestart() {
    if (!connected_) {
        return false;
    }
    const std::uint8_t packet[] = {0x02};
    return socket_.send(packet, sizeof(packet)) == sf::Socket::Status::Done;
}

bool NetworkClient::sendUndo() {
    if (!connected_) {
        return false;
    }
    const std::uint8_t packet[] = {0x03};
    return socket_.send(packet, sizeof(packet)) == sf::Socket::Status::Done;
}

bool NetworkClient::sendSurrender() {
    if (!connected_) {
        return false;
    }
    const std::uint8_t packet[] = {0x04};
    return socket_.send(packet, sizeof(packet)) == sf::Socket::Status::Done;
}

bool NetworkClient::sendCardSelected(int index) {
    if (!connected_) return false;
    const std::uint8_t packet[] = {0x08, static_cast<std::uint8_t>(index)};
    return socket_.send(packet, sizeof(packet)) == sf::Socket::Status::Done;
}

bool NetworkClient::hasRoomConfig() const {
    return receivedConfig_.received;
}

RoomConfig NetworkClient::roomConfig() const {
    return receivedConfig_;
}

bool NetworkClient::hasPendingObstacleSync() const {
    return !pendingObstacleSyncs_.empty();
}

std::vector<sf::Vector2i> NetworkClient::popObstacleSync() {
    auto sync = std::move(pendingObstacleSyncs_.front());
    pendingObstacleSyncs_.erase(pendingObstacleSyncs_.begin());
    return sync;
}
