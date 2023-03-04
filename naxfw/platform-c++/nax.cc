#include "nax.h"

#include "endpoint.h"

namespace nsp {
    namespace tcpip {
        tcp_client::tcp_client() : link_(INVALID_HTCPLINK)
        {
            lwp_once(&tcp_client::tcp_once_, []() {
                ::tcp_init();
            });
        }

        tcp_client::tcp_client(HTCPLINK link) : link_(link)
        {
            lwp_once(&tcp_client::tcp_once_, []() {
                ::tcp_init();
            });
        }

        nsp_status_t tcp_client::create(const std::string& local, const base_tst& tst)
        {
            tst_t obtst;
            obtst.parser_ = tst.get_parser();
            obtst.builder_ = tst.get_builder();
            obtst.cb_ = tst.get_size();

            nsp::tcpip::endpoint ep;
            nsp_status_t status = nsp::tcpip::endpoint::build(local, ep);
            if (NSP_SUCCESS(status)) {
                link_ = tcp_create2(&tcp_client::tcp_io_callback, ep.ipv4(), ep.port(), &obtst);
            } else {
                if (0 == local.size()) {
                    link_ = tcp_create2(&tcp_client::tcp_io_callback, NULL, 0, &obtst);
                } else {
                    // maybe IPC
                    link_ = tcp_create2(&tcp_client::tcp_io_callback, local.c_str(), 0, &obtst);
                }
            }

            return INVALID_HTCPLINK == link_ ? NSP_STATUS_FATAL : NSP_STATUS_SUCCESSFUL;
        }

        nsp_status_t tcp_client::create(const base_tst& tst)
        {
            return create("", tst);
        }

        void tcp_client::destroy()
        {
            if (INVALID_HTCPLINK == link_) {
                ::tcp_destroy(link_);
                link_ = INVALID_HTCPLINK;
            }
        }

    } // tcpip
} // nsp
