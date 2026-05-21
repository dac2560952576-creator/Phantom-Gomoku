#include "NetworkServer.hpp"

bool NetworkServer::start(unsigned short port) {
    listener_.setBlocking(false);
    if (listener_.listen(port) != sf::Socket::Status::Done) {
        return false;
    }
    return true;
}

void NetworkServer::stop() {
    client_.disconnect();
    listener_.close();
    clientConnected_ = false;
    pendingMoves_.clear();
    recvBuffer_.clear();
}

void NetworkServer::update() {
    if (!clientConnected_) {
        if (listener_.accept(client_) == sf::Socket::Status::Done) {
            client_.setBlocking(false);
            clientConnected_ = true;
        }
        return;
    }

    std::array<std::uint8_t, 128> chunk{};
    std::size_t received = 0;
    sf::Socket::Status recvStatus;
    while ((recvStatus = client_.receive(chunk.data(), chunk.size(), received)) == sf::Socket::Status::Done) {
        for (std::size_t i = 0; i < received; ++i) {
            recvBuffer_.push_back(chunk[i]);
        }
    }
    if (recvStatus == sf::Socket::Status::Disconnected || recvStatus == sf::Socket::Status::Error) {
        client_.disconnect();
        clientConnected_ = false;
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

    if (recvBuffer_.size() >= 2 && recvBuffer_[0] == 0x08) {
        NetMove cardSelect;
        cardSelect.row = -5;
        cardSelect.col = static_cast<int>(recvBuffer_[1]);
        pendingMoves_.push_back(cardSelect);
        recvBuffer_.erase(recvBuffer_.begin(), recvBuffer_.begin() + 2);
    }

}

bool NetworkServer::isConnected() const {
    return clientConnected_;
}

bool NetworkServer::hasPendingMove() const {
    return !pendingMoves_.empty();
}

NetMove NetworkServer::popMove() {
    auto move = pendingMoves_.front();
    pendingMoves_.erase(pendingMoves_.begin());
    return move;
}

bool NetworkServer::sendMove(int row, int col) {
    if (!clientConnected_) {
        return false;
    }
    const std::uint8_t packet[] = {0x01, static_cast<std::uint8_t>(row), static_cast<std::uint8_t>(col)};
    return client_.send(packet, sizeof(packet)) == sf::Socket::Status::Done;
}

bool NetworkServer::sendRestart() {
    if (!clientConnected_) {
        return false;
    }
    const std::uint8_t packet[] = {0x02};
    return client_.send(packet, sizeof(packet)) == sf::Socket::Status::Done;
}

bool NetworkServer::sendUndo() {
    if (!clientConnected_) {
        return false;
    }
    const std::uint8_t packet[] = {0x03};
    return client_.send(packet, sizeof(packet)) == sf::Socket::Status::Done;
}

bool NetworkServer::sendSurrender() {
    if (!clientConnected_) {
        return false;
    }
    const std::uint8_t packet[] = {0x04};
    return client_.send(packet, sizeof(packet)) == sf::Socket::Status::Done;
}

bool NetworkServer::sendRoomConfig(int mode, int undoCount, int turnTime, int selectedMapIndex, int obstacleDynamic) {
    if (!clientConnected_) {
        return false;
    }
    const std::uint8_t packet[] = {0x05,
                                   static_cast<std::uint8_t>(mode),
                                   static_cast<std::uint8_t>(undoCount),
                                   static_cast<std::uint8_t>(turnTime),
                                   static_cast<std::uint8_t>(selectedMapIndex),
                                   static_cast<std::uint8_t>(obstacleDynamic)};
    return client_.send(packet, sizeof(packet)) == sf::Socket::Status::Done;
}

bool NetworkServer::sendObstacleSync(const std::vector<sf::Vector2i>& positions) {
    if (!clientConnected_) {
        return false;
    }
    const auto count = static_cast<std::uint8_t>(positions.size());
    std::vector<std::uint8_t> packet;
    packet.push_back(0x06);
    packet.push_back(count);
    for (const auto& pos : positions) {
        packet.push_back(static_cast<std::uint8_t>(pos.x));
        packet.push_back(static_cast<std::uint8_t>(pos.y));
    }
    return client_.send(packet.data(), packet.size()) == sf::Socket::Status::Done;
}

bool NetworkServer::sendCardEvent(int card0, int card1, int card2) {
    if (!clientConnected_) return false;
    const std::uint8_t packet[] = {0x07,
                                   static_cast<std::uint8_t>(card0),
                                   static_cast<std::uint8_t>(card1),
                                   static_cast<std::uint8_t>(card2)};
    return client_.send(packet, sizeof(packet)) == sf::Socket::Status::Done;
}

bool NetworkServer::sendCardSelected(int index) {
    if (!clientConnected_) return false;
    const std::uint8_t packet[] = {0x08, static_cast<std::uint8_t>(index)};
    return client_.send(packet, sizeof(packet)) == sf::Socket::Status::Done;
}

bool NetworkServer::sendWheelResult(bool hostPicks) {
    if (!clientConnected_) return false;
    const std::uint8_t packet[] = {0x0A, static_cast<std::uint8_t>(hostPicks ? 1 : 0)};
    return client_.send(packet, sizeof(packet)) == sf::Socket::Status::Done;
}

bool NetworkServer::sendWheelStartAngle(int startAngle) {
    if (!clientConnected_) return false;
    const std::uint8_t packet[] = {0x0B,
                                   static_cast<std::uint8_t>(startAngle % 256),
                                   static_cast<std::uint8_t>(startAngle / 256)};
    return client_.send(packet, sizeof(packet)) == sf::Socket::Status::Done;
}

bool NetworkServer::sendCardEffectSeed(std::uint32_t seed) {
    if (!clientConnected_) return false;
    const std::uint8_t packet[] = {0x0C,
                                   static_cast<std::uint8_t>(seed & 0xFF),
                                   static_cast<std::uint8_t>((seed >> 8) & 0xFF),
                                   static_cast<std::uint8_t>((seed >> 16) & 0xFF),
                                   static_cast<std::uint8_t>((seed >> 24) & 0xFF)};
    return client_.send(packet, sizeof(packet)) == sf::Socket::Status::Done;
}

std::string NetworkServer::localAddress() const {
    const auto addr = sf::IpAddress::getLocalAddress();
    return addr.has_value() ? addr->toString() : "127.0.0.1";
}
