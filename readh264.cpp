#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
using namespace std;

struct Frame {
    string data;
    bool   key;
};

vector<Frame> frames;
string        sps;
string        pps;

void InputFile()
{
    ifstream      in("1080p_anime.h264");
    ostringstream tmp;
    tmp << in.rdbuf();
    string str = tmp.str();
    cout << str.length() << endl;

    char*        c1 = "\x00\x00\x00\x01";
    const string code1(c1, 4);
    char*        c2 = "\x00\x00\x01";
    const string code2(c2, 3);

    int index  = 0;
    int maxlen = 0;
    while (index < str.length()) {
        int start1 = str.find(code1, index);
        int start2 = str.find(code2, index);

        int start = min(start1, start2);
        if (start == string::npos) {
            break;
        }
        int end1 = str.find(code1, start + 4);
        int end2 = str.find(code2, start + 4);
        int end  = min(end1, end2);

        if (char(str[start + 2]) == 0x00) {
            int naluType = str[start + 4] & 0x1f;
            if (naluType == 7) {
                sps = str.substr(start, end - start);
            } else if (naluType == 8) {
                pps = str.substr(start, end - start);
            } else if (naluType == 5) {
                // may no
                frames.push_back({str.substr(start, end - start), true});
            } else if (naluType == 1) {
                frames.push_back({str.substr(start, end - start), false});
            }
        } else if (char(str[start + 2]) == 0x01) {
            int naluType = str[start + 3] & 0x1f;
            if (naluType == 5) {
                frames.push_back({str.substr(start, end - start), true});
            } else if (naluType != 6) {
                frames.push_back({str.substr(start, end - start), false});
            }
        }
        maxlen = max(maxlen, end - start);
        index  = end;
    }
    cout << sps.size() << endl;
    cout << pps.size() << endl;
    cout << frames.size() << endl;
    cout << maxlen << endl;
}

int  copyTime = 1;
void CopyTest()
{
    int  readTime = 100;
    char buffer[210000];
    cout << "copy start\n";
    for (int k = 0; k != readTime; ++k) {
        for (const auto& frame : frames) {
            for (int i = 0; i != copyTime; ++i) {
                if (frame.key) {
                    memcpy(buffer, sps.c_str(), sps.length());
                    memcpy(buffer + sps.length(), pps.c_str(), pps.length());
                    memcpy(buffer + sps.length() + pps.length(), frame.data.c_str(), frame.data.length());
                } else {
                    memcpy(buffer, frame.data.c_str(), frame.data.length());
                }
            }
            usleep(33 * 1000);
        }
    }
    cout << "copy done\n";
}

int main(int argc, char* argv[])
{
    if (argc != 3) {
        cout << "please input threadNum and copyTime\n";
        return -1;
    }
    int threadNum = atoi(argv[1]);
    copyTime = atoi(argv[2]);
    cout << "press [enter] for start reading" << endl;
    getchar();
    InputFile();
    cout << "press [enter] for start copying" << endl;
    getchar();
    vector<thread> threads;
    for (int i = 0; i < threadNum; ++i) {
        threads.push_back(thread(&CopyTest));
    }

    cout << "press [enter] for end copying" << endl;
    for (auto& thread : threads) {
        thread.join();
    }
    getchar();
    return 0;
}
