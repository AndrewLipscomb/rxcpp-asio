#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/strand.hpp>
#include "./rx-asio.hpp"
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
//------------------------------------------------------------------------------
// Report a failure
void fail(beast::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}
class BeastRxException : public std::exception
{
    private:
        beast::error_code ec_;
        std::string message_;
    public:
        BeastRxException(beast::error_code ec, char const* what)
        : ec_(ec)
        , message_(std::string{what})
        {}
        virtual const char* what() const noexcept override { return message_.c_str(); }
};
// Performs an HTTP GET and prints the response
class session : public std::enable_shared_from_this<session>
{
    tcp::resolver resolver_;
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_; // (Must persist between reads)
    http::request<http::empty_body> req_;
    http::response<http::string_body> res_;
public:
    // Objects are constructed with a strand to
    // ensure that handlers do not execute concurrently.
    explicit
    session(net::strand<net::any_io_executor>& ioc)
    : resolver_(ioc)
    , stream_(ioc)
    {
    }
    // Start the asynchronous operation
    void
    run(
        char const* host,
        char const* port,
        char const* target,
        int version
    )
    {
        // Set up an HTTP GET request message
        req_.version(version);
        req_.method(http::verb::get);
        req_.target(target);
        req_.set(http::field::host, host);
        req_.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        // Look up the domain name
        resolver_.async_resolve(
            host,
            port,
            beast::bind_front_handler(
                &session::on_resolve,
                shared_from_this()));
    }
    void on_resolve(beast::error_code ec, tcp::resolver::results_type results)
    {
        if(ec)
        {
            throw BeastRxException(ec, "resolve");
        }
        // Set a timeout on the operation
        stream_.expires_after(std::chrono::seconds(30));
        // Make the connection on the IP address we get from a lookup
        stream_.async_connect(
            results,
            beast::bind_front_handler(
                &session::on_connect,
                shared_from_this()));
    }
    void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type)
    {
        if(ec)
        {
            throw BeastRxException(ec, "connect");
        }
        // Set a timeout on the operation
        stream_.expires_after(std::chrono::seconds(30));
        // Send the HTTP request to the remote host
        http::async_write(stream_, req_,
            beast::bind_front_handler(
                &session::on_write,
                shared_from_this()));
    }
    void on_write(
        beast::error_code ec,
        std::size_t bytes_transferred
    )
    {
        boost::ignore_unused(bytes_transferred);
        if(ec)
            return fail(ec, "write");
        // Receive the HTTP response
        http::async_read(stream_, buffer_, res_,
            beast::bind_front_handler(
                &session::on_read,
                shared_from_this()));
    }
    void on_read(
        beast::error_code ec,
        std::size_t bytes_transferred
    )
    {
        boost::ignore_unused(bytes_transferred);
        if(ec)
        {
            throw BeastRxException(ec, "read");
        }
        // Write the message to standard out
        std::cout << res_ << std::endl;
        // Gracefully close the socket
        stream_.socket().shutdown(tcp::socket::shutdown_both, ec);
        // not_connected happens sometimes so don't bother reporting it.
        if(ec && ec != beast::errc::not_connected)
        {
            throw BeastRxException(ec, "shutdown");
        }
        // If we get here then the connection is closed gracefully
    }
};


// class OkBuffer : public std::string
// {
//     public:
//         using std::string::string;

//         bool operator== (const std::string&) = delete;
// };

class OkTuple : public std::tuple<std::size_t, boost::beast::http::response<http::string_body> >
{
    public:
        using std::tuple<std::size_t, boost::beast::http::response<http::string_body> >::tuple;

        bool operator==(const OkTuple&) = delete;
};

bool operator==(const OkTuple&, const OkTuple&) = delete;

//------------------------------------------------------------------------------
int main(int argc, char** argv)
{
    // Check command line arguments.
    if(argc != 4 && argc != 5)
    {
        std::cerr <<
            "Usage: http-client-async <host> <port> <target> [<HTTP version: 1.0 or 1.1(default)>]\n" <<
            "Example:\n" <<
            "    http-client-async www.example.com 80 /\n" <<
            "    http-client-async www.example.com 80 / 1.0\n";
        return EXIT_FAILURE;
    }
    auto const host = argv[1];
    auto const port = argv[2];
    auto const target = argv[3];
    int version = 11; //argc == 5 && !std::strcmp("1.0", argv[4]) ? 10 : 11;
    int req_count = std::max(std::atoi(argv[4]), 1);
    // The io_context is required for all I/O
    net::io_context ioc;
    // rxcpp::operators::delay()
    using namespace rxcpp;
    using namespace rxcpp::operators;

    typedef std::tuple< tcp::resolver::results_type::endpoint_type, std::shared_ptr<beast::tcp_stream> > Intermediate1_t;
    typedef std::tuple< std::size_t, std::shared_ptr<beast::tcp_stream> > Intermediate2_t;


    std::vector<rxcpp::composite_subscription> requests;

    for (int i = 0; i < req_count; ++i)
    {
        net::strand<net::any_io_executor> strand { net::make_strand(ioc) };
        auto request = rxcpp::observable<>::create< tcp::resolver::results_type >( [&](rxcpp::subscriber< tcp::resolver::results_type > s){
            auto resolver = std::make_shared<tcp::resolver>(strand);
            resolver->async_resolve(
                host, port,
                [s{std::move(s)}, resolver](beast::error_code ec, tcp::resolver::results_type results) {
                    if (ec)
                    {
                        s.on_error(std::make_exception_ptr(BeastRxException(ec, "Resolve")));
                        return;
                    }

                    s.on_next(results);
                    s.on_completed();
                }
            );
        })
        .flat_map( [&strand](tcp::resolver::results_type results) {
            auto stream = std::make_shared<beast::tcp_stream>(strand);
            // Set a timeout on the operation
            stream->expires_after(std::chrono::seconds(30));

            return rxcpp::observable<>::create< Intermediate1_t >(
                [stream, results{std::move(results)}](rxcpp::subscriber< Intermediate1_t > s){
                    stream->async_connect(
                        results,
                        [stream, s{std::move(s)}](beast::error_code ec, tcp::resolver::results_type::endpoint_type endpoint) {
                            if (ec)
                            {
                                s.on_error(std::make_exception_ptr(BeastRxException(ec, "Connect")));
                                return;
                            }

                            s.on_next(std::make_tuple(std::move(endpoint), stream));
                            s.on_completed();
                        }
                    );
                }
            )
            ;
        })
        .flat_map( [&strand, &version, &target, &host](Intermediate1_t last){
            return rxcpp::observable<>::create< Intermediate2_t >(
                [last{std::move(last)}, &version, &target, &host](rxcpp::subscriber< Intermediate2_t > s){
                    std::shared_ptr<beast::tcp_stream> stream { std::get<1>(last) };
                    // Send the HTTP request to the remote host
                    auto req = std::make_shared< http::request<http::empty_body> >();
                    req->version(version);
                    req->method(http::verb::get);
                    req->target(target);
                    req->set(http::field::host, host);
                    req->set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

                    http::async_write(
                        *stream,
                        *req,
                        [s{std::move(s)}, stream, req](beast::error_code ec, std::size_t bytes_transferred) {
                            if (ec)
                            {
                                s.on_error(std::make_exception_ptr(BeastRxException(ec, "Write")));
                                return;
                            }

                            s.on_next(std::make_tuple(std::move(bytes_transferred), stream));
                            s.on_completed();
                        }
                    );
                }
            )
            ;
        })
        .flat_map( [&strand](Intermediate2_t last){
            return rxcpp::observable<>::create< OkTuple >(
                [last{std::move(last)}](rxcpp::subscriber< OkTuple > s){
                    std::shared_ptr<beast::tcp_stream> stream { std::get<1>(last) };

                    auto buffer = std::make_shared<boost::beast::flat_buffer>();
                    auto res = std::make_shared< http::response<http::string_body> >();

                    http::async_read(
                        *stream,
                        *buffer,
                        *res,
                        [s{std::move(s)}, stream, buffer, res](beast::error_code ec, std::size_t bytes_transferred) {
                            if (ec)
                            {
                                s.on_error(std::make_exception_ptr(BeastRxException(ec, "Read")));
                                return;
                            }

                            s.on_next(OkTuple{std::move(bytes_transferred), std::move(*res)});
                            s.on_completed();
                        }
                    );
                }
            )
            ;
        })
        .observe_on( observe_on_asio(strand) )
        .subscribe(
            [i](OkTuple v){
                std::cout << "Dunzo Req(" << i << ") - " << std::get<1>(v) << std::endl;
                    // << std::get<1>(v)
                    // << " (" << std::get<0>(v) << ")"
                    // <<  std::endl;
            },
            [](std::exception_ptr ep){
                try
                {
                    std::rethrow_exception(ep);
                }
                catch (const std::exception& ex)
                {
                    printf("OnError: %s\n", ex.what());
                }
            },
            [](){
                printf("OnCompleted\n");
            }
        );

        requests.emplace_back(
            std::move(request)
        );

        auto simple_poop = rxcpp::observable<>::create< int >(
            [](rxcpp::subscriber< int > s){
                s.on_next(5);
                s.on_completed();
            }
        )
        .delay(std::chrono::seconds(3), observe_on_asio(strand))
        .observe_on( observe_on_asio(strand) )
        .subscribe(
            [](int v){
                std::cout << "Dunzo Int(" << v << ")" << std::endl;
            },
            [](std::exception_ptr ep){
                try
                {
                    std::rethrow_exception(ep);
                }
                catch (const std::exception& ex)
                {
                    printf("OnError: %s\n", ex.what());
                }
            },
            [](){
                printf("OnCompleted\n");
            }
        )
        ;

        requests.emplace_back(
            std::move(simple_poop)
        );

    }


    ;



    // Launch the asynchronous operation
    // std::make_shared<rx_session>(strand)->run(host, port, target, version);
    // Run the I/O service. The call will return when
    // the get operation is complete.
    ioc.run();
    return EXIT_SUCCESS;
}