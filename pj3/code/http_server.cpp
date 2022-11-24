#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <memory>
#include <utility>
#include <boost/asio.hpp>

using namespace std;
using boost::asio::ip::tcp;
using boost::asio::io_context;

io_context glo_ioc;

class session
    : public std::enable_shared_from_this<session>
{
public:
    session(tcp::socket socket)
        : socket_(std::move(socket))
    {
    }

    void start()
    {
        do_read();
    }

private:
    void do_read()
    {
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length)
            {
                sscanf(data_, "%s %s %s %s %s", REQUEST_METHOD, REQUEST_URI, SERVER_PROTOCOL, blackhole, HTTP_HOST);
                if (!ec)
                {
                    do_write(length);
                }
            });
    }

    void do_write(std::size_t length)
    {
        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(data_, 0),
            [this, self](boost::system::error_code ec, std::size_t /*length*/)
            {
                if (!ec)
                {
                    strcpy(SERVER_ADDR, socket_.local_endpoint().address().to_string().c_str());
                    sprintf(SERVER_PORT, "%u", socket_.local_endpoint().port());
                    strcpy(REMOTE_ADDR, socket_.remote_endpoint().address().to_string().c_str());
                    sprintf(REMOTE_PORT, "%u", socket_.remote_endpoint().port());

                    bool flag = false;
                    int j = 0;
                    for (int i = 0; i < 500; i++) 
                    {
                        if (REQUEST_URI[i] == '\0')
                            break;
                        else if (REQUEST_URI[i] == '?') 
                        {
                            flag = true;
                            continue;
                        }
                        if (flag)
                            QUERY_STRING[j++] = REQUEST_URI[i];
                    }

                    for (int i = 1; i < 50; i++) 
                    {
                        if (REQUEST_URI[i] == '\0' || REQUEST_URI[i] == '?')
                            break;
                        EXEC_FILE[i + 1] = REQUEST_URI[i];
                    }

                    setenv("REQUEST_METHOD", REQUEST_METHOD, 1);
                    setenv("REQUEST_URI", REQUEST_URI, 1);
                    setenv("QUERY_STRING", QUERY_STRING, 1);
                    setenv("SERVER_PROTOCOL", SERVER_PROTOCOL, 1);
                    setenv("HTTP_HOST", HTTP_HOST, 1);
                    setenv("SERVER_ADDR", SERVER_ADDR, 1);
                    setenv("SERVER_PORT", SERVER_PORT, 1);
                    setenv("REMOTE_ADDR", REMOTE_ADDR, 1);
                    setenv("REMOTE_PORT", REMOTE_PORT, 1);
                    setenv("EXEC_FILE", EXEC_FILE, 1);

					socket_.send(boost::asio::buffer(string("HTTP/1.1 200 OK\r\n")));

                    glo_ioc.notify_fork(io_context::fork_prepare);
                    if (fork() > 0) 
                    {
                        glo_ioc.notify_fork(io_context::fork_parent);
                        socket_.close();
                    } 
                    else 
                    {
                        glo_ioc.notify_fork(io_context::fork_child);
                        int sock = socket_.native_handle();
                        dup2(sock, STDERR_FILENO);
                        dup2(sock, STDIN_FILENO);
                        dup2(sock, STDOUT_FILENO);
                        socket_.close();

                        if (execlp(EXEC_FILE, EXEC_FILE, NULL) < 0)
                            cout << "Content-type:text/html\r\n\r\n<h1>FAIL</h1>";
                    }

                    do_read();
                }
            });
    }

    tcp::socket socket_;
    enum { max_length = 1024 };
    char data_[max_length];
    char REQUEST_METHOD[20];
    char REQUEST_URI[500];
    char QUERY_STRING[500];
    char SERVER_PROTOCOL[50];
    char HTTP_HOST[100];
    char SERVER_ADDR[50];
    char SERVER_PORT[10];
    char REMOTE_ADDR[50];
    char REMOTE_PORT[10];
    char blackhole[20];
    char EXEC_FILE[50] = "./";

};

class server
{
public:
    server(short port)
        : acceptor_(glo_ioc, tcp::endpoint(tcp::v4(), port))
    {
        do_accept();
    }

private:
    void do_accept()
    {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket)
            {
                if (!ec)
                {
                    std::make_shared<session>(std::move(socket))->start();
                }
                do_accept();
            });
    }

    tcp::acceptor acceptor_;
};

int main(int argc, char *argv[])
{
	cout << "server start\n";
    try
    {
        if (argc != 2)
        {
            cerr << "Usage: async_tcp_echo_server <port>\n";
            return 1;
        }

        server s(atoi(argv[1]));
        glo_ioc.run();
    }
    catch (exception& e)
    {
        cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
