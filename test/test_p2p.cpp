/**
 * Copyright (c) 2018 Cornell University.
 *
 * Author: Ted Yin <tederminant@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <cstdio>
#include <cstdint>
#include <string>
#include <functional>
#include <unordered_map>

#include "salticidae/msg.h"
#include "salticidae/event.h"
#include "salticidae/network.h"
#include "salticidae/stream.h"

using salticidae::NetAddr;
using salticidae::DataStream;
using salticidae::PeerNetwork;
using salticidae::htole;
using salticidae::letoh;
using salticidae::EventContext;
using salticidae::ThreadCall;
using std::placeholders::_1;
using std::placeholders::_2;

struct Net {
    uint64_t id;
    EventContext ec;
    ThreadCall tc;
    std::thread th;
    PeerNetwork<uint8_t> *net;
    const std::string listen_addr;

    Net(uint64_t id, uint16_t port): id(id), tc(ec), listen_addr("127.0.0.1:"+ std::to_string(port)) {
        net = new salticidae::PeerNetwork<uint8_t>(
            ec,
            salticidae::PeerNetwork<uint8_t>::Config().conn_timeout(5).ping_period(2));
        net->reg_error_handler([this](const std::exception &err, bool fatal) {
            fprintf(stdout, "net %lu: captured %s error during an async call: %s\n", this->id, fatal ? "fatal" : "recoverable", err.what());
        });
        th = std::thread([=](){
            try {
                net->start();
                net->listen(NetAddr(listen_addr));
                fprintf(stdout, "net %lu: listen to %s\n", id, listen_addr.c_str());
                ec.dispatch();
            } catch (std::exception &err) {
                fprintf(stdout, "net %lu: got error during a sync call: %s\n", id, err.what());
            }
            fprintf(stdout, "net %lu: main loop ended\n", id);
        });
    }

    void add_peer(const std::string &listen_addr) {
        try {
            net->add_peer(NetAddr(listen_addr));
        } catch (std::exception &err) {
            fprintf(stdout, "net %lu: got error during a sync call: %s\n", id, err.what());
        }
    }

    void del_peer(const std::string &listen_addr) {
        try {
            net->del_peer(NetAddr(listen_addr));
        } catch (std::exception &err) {
            fprintf(stdout, "net %lu: got error during a sync call: %s\n", id, err.what());
        }
    }

    void stop_join() {
        tc.async_call([ec=this->ec](ThreadCall::Handle &) { ec.stop(); });
        th.join();
    }

    ~Net() { delete net; }
};

std::unordered_map<uint64_t, Net *> nets;
std::unordered_map<std::string, std::function<void(char *)> > cmd_map;

int read_int(char *buff) {
    scanf("%64s", buff);
    try {
        int t = std::stoi(buff);
        if (t < 0) throw std::invalid_argument("negative");
        return t;
    } catch (std::invalid_argument) {
        fprintf(stdout, "expect a non-negative integer\n");
        return -1;
    }
}

int main(int argc, char **argv) {
    int i;
    fprintf(stdout, "p2p network library playground (type help for more info)\n");
    fprintf(stdout, "========================================================\n");

    auto cmd_exit = [](char *) {
        for (auto &p: nets)
            p.second->stop_join();
        exit(0);
    };

    auto cmd_add = [](char *buff) {
        int id = read_int(buff);
        if (id < 0) return;
        if (nets.count(id))
        {
            fprintf(stdout, "net id already exists\n");
            return;
        }
        int port = read_int(buff);
        if (port < 0) return;
        if (port >= 65536)
        {
            fprintf(stdout, "port should be < 65536\n");
            return;
        }
        nets.insert(std::make_pair(id, new Net(id, port)));
    };

    auto cmd_ls = [](char *) {
        for (auto &p: nets)
            fprintf(stdout, "%d\n", p.first);
    };

    auto cmd_del = [](char *buff) {
        int id = read_int(buff);
        if (id < 0) return;
        auto it = nets.find(id);
        if (it == nets.end())
        {
            fprintf(stdout, "net id does not exist\n");
            return;
        }
        it->second->stop_join();
        delete it->second;
        nets.erase(it);
    };

    auto cmd_addpeer = [](char *buff) {
        int id = read_int(buff);
        if (id < 0) return;
        auto it = nets.find(id);
        if (it == nets.end())
        {
            fprintf(stdout, "net id does not exist\n");
            return;
        }
        int id2 = read_int(buff);
        if (id2 < 0) return;
        auto it2 = nets.find(id2);
        if (it2 == nets.end())
        {
            fprintf(stdout, "net id does not exist\n");
            return;
        }
        it->second->add_peer(it2->second->listen_addr);
    };

    auto cmd_delpeer = [](char *buff) {
        int id = read_int(buff);
        if (id < 0) return;
        auto it = nets.find(id);
        if (it == nets.end())
        {
            fprintf(stdout, "net id does not exist\n");
            return;
        }
        int id2 = read_int(buff);
        if (id2 < 0) return;
        auto it2 = nets.find(id2);
        if (it2 == nets.end())
        {
            fprintf(stdout, "net id does not exist\n");
            return;
        }
        it->second->del_peer(it2->second->listen_addr);
    };

    auto cmd_help = [](char *) {
        fprintf(stdout,
            "add <node-id> <port> -- start a node (create a PeerNetwork instance)\n"
            "addpeer <node-id> <peer-id> -- add a peer to a given node\n"
            "rmpeer <node-id> <peer-id> -- add a peer to a given node\n"
            "rm <node-id> -- remove a node (destroy a PeerNetwork instance)\n"
            "ls -- list all node ids\n"
            "exit -- quit the program\n"
            "help -- show this info\n"
        );
    };

    cmd_map.insert(std::make_pair("add", cmd_add));
    cmd_map.insert(std::make_pair("addpeer", cmd_addpeer));
    cmd_map.insert(std::make_pair("del", cmd_del));
    cmd_map.insert(std::make_pair("delpeer", cmd_delpeer));
    cmd_map.insert(std::make_pair("ls", cmd_ls));
    cmd_map.insert(std::make_pair("exit", cmd_exit));
    cmd_map.insert(std::make_pair("help", cmd_help));

    for (;;)
    {
        fprintf(stdout, "> ");
        char buff[128];
        if (scanf("%64s", buff) == EOF) break;
        auto it = cmd_map.find(buff);
        if (it == cmd_map.end())
            fprintf(stdout, "invalid comand \"%s\"\n", buff);
        else
            (it->second)(buff);
    }

    return 0;
}
