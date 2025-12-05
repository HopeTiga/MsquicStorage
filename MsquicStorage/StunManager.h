#ifndef STUN_MANAGER_HPP
#define STUN_MANAGER_HPP

#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>
#include <random>
#include <vector>
#include <string>
#include <cstdint>

class StunManager
{
public:
    // 构造即绑定本地端口，但不发起请求
    explicit StunManager(boost::asio::io_context& io, unsigned short localPort = 0)
        : socket(io, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), localPort)),
        localPort(socket.local_endpoint().port())
    {
    }

    // 异步向给定 STUN 服务器打洞，成功返回 true 并填充 publicIp / publicPort
    boost::asio::awaitable<bool> punch(std::string_view host, std::string_view port)
    {
        try
        {
            boost::asio::ip::udp::resolver res(socket.get_executor());
            auto results = co_await res.async_resolve(host, port, boost::asio::use_awaitable);
            boost::asio::ip::udp::endpoint stunEp = *results.begin();

            // 构造 Binding Request
            std::vector<std::uint8_t> req = buildBindingRequest();

            co_await socket.async_send_to(boost::asio::buffer(req), stunEp, boost::asio::use_awaitable);

            std::vector<std::uint8_t> reply(512);
            boost::asio::ip::udp::endpoint sender;
            std::size_t n = co_await socket.async_receive_from(boost::asio::buffer(reply), sender, boost::asio::use_awaitable);
            reply.resize(n);

            co_return parseBindingResponse(reply, publicIp, publicPort);
        }
        catch (...)
        {
            co_return false;
        }
    }

    // 拿到上一次 punch 成功的公网地址；若未成功，内容为空
    std::string getPublicIp() const { return publicIp; }
    std::uint16_t getPublicPort() const { return publicPort; }
    unsigned short getLocalPort() const { return localPort; }

private:
    static constexpr std::uint32_t magicCookie = 0x2112A442;

    struct Header
    {
        std::uint16_t type;
        std::uint16_t length;
        std::uint32_t cookie;
        std::uint8_t  txId[12];
    };

    boost::asio::ip::udp::socket socket;
    unsigned short localPort;
    std::string publicIp;
    std::uint16_t publicPort = 0;

    // 组装 Binding Request
    static std::vector<std::uint8_t> buildBindingRequest()
    {
        Header h{};
        h.type = boost::endian::native_to_big(std::uint16_t(0x0001)); // Binding Request
        h.length = 0;
        h.cookie = boost::endian::native_to_big(magicCookie);

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 255);
        for (auto& b : h.txId) b = static_cast<std::uint8_t>(dist(gen));

        std::vector<std::uint8_t> buf(sizeof h);
        std::memcpy(buf.data(), &h, sizeof h);
        return buf;
    }

    // 解析 Binding Response
    static bool parseBindingResponse(const std::vector<std::uint8_t>& buf,
        std::string& outIp,
        std::uint16_t& outPort)
    {
        if (buf.size() < sizeof(Header)) return false;
        auto& h = *reinterpret_cast<const Header*>(buf.data());
        if (boost::endian::big_to_native(h.type) != 0x0101) return false; // Success

        std::size_t offset = sizeof(Header);
        while (offset + 4 <= buf.size())
        {
            std::uint16_t attrType = boost::endian::big_to_native(
                *reinterpret_cast<const std::uint16_t*>(&buf[offset]));
            std::uint16_t attrLen = boost::endian::big_to_native(
                *reinterpret_cast<const std::uint16_t*>(&buf[offset + 2]));
            if (attrType == 0x0020 && attrLen >= 8) // XOR-MAPPED-ADDRESS
            {
                if (buf[offset + 5] == 0x01) // IPv4
                {
                    std::uint16_t xPort = boost::endian::big_to_native(
                        *reinterpret_cast<const std::uint16_t*>(&buf[offset + 6]));
                    std::uint32_t xIp = boost::endian::big_to_native(
                        *reinterpret_cast<const std::uint32_t*>(&buf[offset + 8]));
                    outPort = xPort ^ (magicCookie >> 16);
                    std::uint32_t ipVal = xIp ^ magicCookie;
                    outIp = std::to_string((ipVal >> 24) & 0xFF) + "." +
                        std::to_string((ipVal >> 16) & 0xFF) + "." +
                        std::to_string((ipVal >> 8) & 0xFF) + "." +
                        std::to_string(ipVal & 0xFF);
                    return true;
                }
            }
            offset += 4 + attrLen;
            if (attrLen & 0x03) offset += 4 - (attrLen & 0x03);
        }
        return false;
    }
};

#endif // STUN_MANAGER_HPP