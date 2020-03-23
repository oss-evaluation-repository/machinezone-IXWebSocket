/*
 * Copyright (c) 2014, Rex
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of the {organization} nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  IXStatsdClient.cpp
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2020 Machine Zone, Inc. All rights reserved.
 */

// Adapted from statsd-client-cpp
// test with netcat as a server: `nc -ul 8125`

#include "IXStatsdClient.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

namespace ix
{
    StatsdClient::StatsdClient(const string& host,
                               int port,
                               const string& prefix)
    : _host(host)
      , _port(port)
      , _prefix(prefix)
      , _stop(false)
    {
        _thread = std::thread([this] {
            while (!_stop)
            {
                std::deque<std::string> staged_message_queue;

                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    batching_message_queue_.swap(staged_message_queue);
                }

                while (!staged_message_queue.empty())
                {
                    auto message = staged_message_queue.front();
                    _socket.sendto(message);
                    staged_message_queue.pop_front();
                }

                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
    }

    StatsdClient::~StatsdClient()
    {
        _stop = true;
        if (_thread.joinable()) _thread.join();

        _socket.close();
    }

    bool StatsdClient::init(std::string& errMsg)
    {
        return _socket.init(_host, _port, errMsg);
    }

    /* will change the original string */
    void StatsdClient::cleanup(string& key)
    {
        size_t pos = key.find_first_of(":|@");
        while (pos != string::npos)
        {
            key[pos] = '_';
            pos = key.find_first_of(":|@");
        }
    }

    int StatsdClient::dec(const string& key)
    {
        return count(key, -1);
    }

    int StatsdClient::inc(const string& key)
    {
        return count(key, 1);
    }

    int StatsdClient::count(const string& key, size_t value)
    {
        return send(key, value, "c");
    }

    int StatsdClient::gauge(const string& key, size_t value)
    {
        return send(key, value, "g");
    }

    int StatsdClient::timing(const string& key, size_t ms)
    {
        return send(key, ms, "ms");
    }

    int StatsdClient::send(string key, size_t value, const string &type)
    {
        cleanup(key);

        char buf[256];
        snprintf(buf, sizeof(buf), "%s%s:%zd|%s",
                 _prefix.c_str(), key.c_str(), value, type.c_str());

        return send(buf);
    }

    int StatsdClient::send(const string &message)
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if (batching_message_queue_.empty() ||
            batching_message_queue_.back().length() > max_batching_size)
        {
            batching_message_queue_.push_back(message);
        }
        else
        {
            (*batching_message_queue_.rbegin()).append("\n").append(message);
        }

        return 0;
    }
} // end namespace ix
