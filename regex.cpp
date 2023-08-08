// g++ regex.cpp -o regex -lcrypto

#include "openssl/md5.h"
#include <cstdio>
#include <iostream>
#include <queue>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <iomanip>

std::string MD5(const std::string& src)
{
    unsigned char     MD5Hash[MD5_DIGEST_LENGTH];
    std::string       MD5Digest;
    std::string       tmp;
    std::stringstream ss;

    MD5((const unsigned char*)src.c_str(), src.size(), MD5Hash);

    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        // 2位16进制
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)MD5Hash[i] << std::endl;
        ss >> tmp;
        MD5Digest += tmp;
    }

    return MD5Digest;
}

void ParseWfd()
{
    std::string msg =
        "RTSP/1.0 200 OK\r\nCSeq: 1\r\nPublic: org.wfa.wfd1.0, GET_PARAMETER, SET_PARAMETER, SETUP, "
        "PLAY, PAUSE, TEARDOWN\r\n\r\nGET_PARAMETER rtsp://localhost/wfd1.0 RTSP/1.0\r\nCSeq: "
        "1\r\nContent-Length: 130\r\n\r\nContent-Type: text/parameters\r\nwfd_audio_codecs\r\nwfd_video_formats\r\n";

    std::regex               reg{"\r\n\r\n"};
    std::vector<std::string> out{std::sregex_token_iterator(msg.begin(), msg.end(), reg, -1),
                                 std::sregex_token_iterator()};
    for (auto item : out) {
        std::cout << item << std::endl << std::endl;
    }
}

std::string Trim(const std::string& str)
{
    return str.substr(str.find_first_not_of(' '), str.find_last_not_of(' ') + 1 - str.find_first_not_of(' '));
}

void ParseRtsp()
{
    std::string              msg = "Response: RTSP/1.0 401 Unauthorized\r\n"
                                   "CSeq: 3\r\n"
                                   "WWW-Authenticate: Basic realm=\"IP Camera\"\r\n"
                                   "WWW-Authenticate: Digest realm=\"IP Camera(C8121)\", "
                                   "nonce=\"3559b2d4e6208048ffafbd8f6f69052f\", stale=\"FALSE\"\r\n"
                                   "Date:  Sat, Feb 21 1970 13:35:11 GMT\r\n"
                                   "\r\n";
    std::regex               reg{"\r\n"};
    std::vector<std::string> out{std::sregex_token_iterator(msg.begin(), msg.end(), reg, -1),
                                 std::sregex_token_iterator()};
    for (auto item : out) {
        std::cout << item << std::endl << std::endl;
    }

    std::unordered_map<std::string, std::string> header;

    for (int i = 0; i < (int)out.size(); ++i) {
        if (out[i].size() > 3) {
            auto index = out[i].find_first_of(':');
            if (index != 0 && index + 1 != out[i].length()) {
                auto token = Trim(out[i].substr(0, index));
                auto value = Trim(out[i].substr(index + 1));
                if (token == "WWW-Authenticate" && value.find("Digest") == std::string::npos) {
                    continue;
                }
                header.emplace(token, value);
                std::cout << "token:*" << token << ", value:*" << value << std::endl;
            }
        }
    }
    auto authenticate = header.at("WWW-Authenticate");
    // std::cout << authenticate;
    std::regex               regg{", "};
    std::vector<std::string> auout{std::sregex_token_iterator(authenticate.begin(), authenticate.end(), regg, -1),
                                   std::sregex_token_iterator()};

    for (auto item : auout) {
        auto separator = item.find('=');
        if (separator != std::string::npos) {
            auto key = item.substr(0, separator);
            if (key == "Digest realm") {
                auto value = item.substr(separator + 2);
                value.pop_back();
                std::cout << "Digest realm: " << value << std::endl;
            } else if (key == "nonce") {
                auto value = item.substr(separator + 2);
                value.pop_back();
                std::cout << "nonce: " << value << std::endl;
            }
        }
    }
}

bool Parse(const std::string& url)
{
    if (url.empty()) {
        return false;
    }

    std::regex  match("^rtsp://(([a-zA-Z0-9]+):([a-zA-Z0-9]+)@)?([a-zA-Z0-9.-]+)(:([0-9]+))?/");
    std::smatch sm;
    if (!std::regex_search(url, sm, match)) {
        printf("Invalid URL\n");
        return false;
    }
    // assert(sm.size() == 7);
    auto        username_ = sm[2].str();
    auto        password_ = sm[3].str();
    auto        host_     = sm[4].str();
    int         port      = std::atoi(sm[6].str().c_str());
    std::string port_     = "";
    if (port > 0) {
        port_ = port;
    }
    auto path_ = sm.suffix().str();

    printf("username: %s\npassword: %s\nhost: %s\nport: %d\npath: %s\n", username_.c_str(), password_.c_str(),
           host_.c_str(), port_.c_str(), path_.c_str());
    return true;
}

// Authorization: Digest username="admin", realm="IP Camera(C8121)", nonce="3559b2d4e6208048ffafbd8f6f69052f",
// uri="rtsp://192.168.63.62:554?username=admin&password=nj123456", response="8f4cb30c7ef16e161bbe81a0c312ec29"\r\n
void Response()
{
    std::string realm    = "IP Camera(C8121)";
    std::string nonce    = "3559b2d4e6208048ffafbd8f6f69052f";
    std::string username = "admin";
    std::string password = "nj123456";
    std::string rtspUrl  = "rtsp://192.168.63.62:554?username=admin&password=nj123456";

    auto urpMD5   = MD5(username + ':' + realm + ':' + password);
    std::cout << urpMD5 << " " << urpMD5.length() << std::endl;
    if (urpMD5 == "8696d231bd07a83e244c0b61407adc76") {
        std::cout << "urp pass\n";
    }
    auto pmuMD5   = MD5("DESCRIBE:" + rtspUrl);
    if (pmuMD5 == "30c26a0ff3b414738bc4be026dfb7030") {
        std::cout << "pmu pass\n";
    }
    std::cout << pmuMD5 << " " << pmuMD5.length() << std::endl;
    auto responce = MD5(urpMD5 + ':' + nonce + ':' + pmuMD5);

    std::cout << responce << std::endl;
}

int main()
{
    ParseRtsp();
    // rtsp://192.168.63.188:554/mpeg4?username=admin&password=7AE44E20D1E6E851767189B0C57CAC64
    // rtsp://192.168.63.62:554?username=admin&password=nj123456
    // Response();
    return 0;
}
