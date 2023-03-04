#pragma once

#include <memory>
#include <string>

#include "nis.h"
#include "threading.h"

namespace nsp {
    namespace tcpip {

        class base_tst
        {
        protected:
            base_tst() {}
            virtual ~base_tst() {}

            virtual tcp_ppt_parser_t get_parser() const = 0;
            virtual tcp_ppt_builder_t get_builder() const = 0;
            virtual int get_size() const = 0;
        };

        class tcp_client : public std::enable_shared_from_this<tcp_client>
        {
            HTCPLINK link_;
            static lwp_once_t tcp_once_;

        public:
            tcp_client();
            tcp_client(HTCPLINK link);

        public:
            nsp_status_t create(const std::string& local, const base_tst& tst);
            nsp_status_t create(const base_tst& tst);
            void destroy();

            nsp_status_t connect(const std::string& target);
            nsp_status_t connect2(const std::string& target);

        protected:
            virtual void on_connected();
            virtual void on_closed();
            virtual void on_recvdata(const std::vector<unsigned char>& data);
        };

        static lwp_once_t tcp_client::tcp_once_ = LWP_ONCE_INIT;

    } // tcpip
} // nsp
