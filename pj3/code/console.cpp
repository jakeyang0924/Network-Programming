#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <memory>
#include <utility>
#include <string>
#include <vector>
#include <array>
#include <fstream>
#include <boost/asio.hpp>

using namespace std;
using namespace boost::asio;
using namespace boost::asio::ip;

io_context ioc;
char host[5][50];
char port[5][10];
char file[5][20];

struct Session
{
    string id;
    string host;
    string port;
    string file;
    vector<string> cmd;
};

class Console : public enable_shared_from_this<Console>
{
    public:
        Console(Session s) : resolv(ioc), tcp_socket(ioc), session(move(s)){}
        void start()
        {
            connect_server();
        }

    private:
        tcp::resolver resolv{ioc};
        tcp::socket tcp_socket{ioc};
        Session session;
        array<char, 1024> bytes;

        void read_handler()
        {
            auto self(shared_from_this());
            tcp_socket.async_read_some(boost::asio::buffer(bytes), [this, self](boost::system::error_code ec, size_t length) {
                if (!ec) 
                {
                    string str = bytes.data();
                    bytes.fill('\0');
                    deal_html(str);
                    cout << "<script>document.getElementById('" << session.id << "').innerHTML += '" << str << "';</script>\n";
					fflush(stdout);
                    if(session.cmd.size())
                    {
                        if(str.find("%") != -1)
                        {
                            string cmd = session.cmd[0];
                            tcp_socket.async_send(boost::asio::buffer(cmd), [this, self, cmd](boost::system::error_code ec, size_t){});
                            deal_html(cmd);
                            cout << "<script>document.getElementById('" << session.id << "').innerHTML += '<b>" << cmd << "</b>';</script>\n";
							fflush(stdout);
                            session.cmd.erase(session.cmd.begin());
                        }
                        read_handler();
                    }
                }
            });
        }

        void connect_server()
        {
            auto self(shared_from_this());
            string file_name = "test_case/" + session.file;

            boost::asio::ip::tcp::resolver::query q{session.host, session.port};
            resolv.async_resolve(q, [this, self](const boost::system::error_code &ec, tcp::resolver::iterator it){
                tcp_socket.async_connect(*it, [this, self](const boost::system::error_code &ec){
                    read_handler();
                });
            });
        }

        void deal_html(string &str)
        {
            for(int i=0; i<str.length(); i++)
            {
                if(str[i] == '\r')
                    str.erase(str.begin()+(i--));
                else if(str[i] == '\n')
                    str.replace(i, 1, "&NewLine;");
                else if(str[i] == '\'')
                    str.replace(i, 1, "&apos;");
                else if(str[i] == '\"')
                    str.replace(i, 1, "&quot;");
                else if(str[i] == '>')
                    str.replace(i, 1, "&gt;");
                else if(str[i] == '<')
                    str.replace(i, 1, "&lt;");
            }
        }
};


int main(int argc, char *argv[])
{
    int session_cnt = 0;
    char q_str[500];
    strcpy(q_str, getenv("QUERY_STRING"));

    char tmp[50];

    for ( int i = 0; i < 5; i++)
    {
        host[i][0] = '\0';
        port[i][0] = '\0';
        file[i][0] = '\0';
    }

    int j = 0, cnt = 0;
    for (int i = 0; i < 500; i++)
    {
        if (q_str[i] == '\0')
            break;
        if (q_str[i] == '&')
        {
            tmp[j] = '\0';
            if (j > 3)
            {
                int r = cnt % 3;
                if (r == 0)
                    strcpy(host[cnt / 3], tmp + 3);
                else if (r == 1)
                    strcpy(port[cnt / 3], tmp + 3);
                else
                    strcpy(file[cnt / 3], tmp + 3);
            }            
            j = 0;
            cnt++;
            continue;
        }
        tmp[j++] = q_str[i];
    }

    /* count the total sessions */
    for (int i = 0; i < 5; i++)
        if (strlen(host[i]))
            session_cnt++;

    /* create session vectors and initialize them */
    vector<Session> s(session_cnt);
    for (int i = 0; i < session_cnt; i++)
    {
        s[i].id = to_string(i);
        s[i].host = string(host[i]);
        s[i].port = string(port[i]);
        s[i].file = string(file[i]);
    }
    for (auto &i : s)
    {
        ifstream fd("./test_case/" + i.file);
        string line;
        while (getline(fd, line))
            i.cmd.push_back(line + "\n");
        fd.close();
    }

    /* print the interface */
    cout << "Content-type: text/html\r\n\r\n";
    cout << "<!DOCTYPE html>\n";
    cout << "<html lang=\"en\">\n";
    cout << "<head>\n";
    cout << "<meta charset=\"UTF-8\" />\n";
    cout << "<title>Console.cgi</title>\n";
    cout << "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\" integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\" crossorigin=\"anonymous\" />\n";
    cout << "<link href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\" rel=\"stylesheet\" />\n";
    cout << "<link rel=\"icon\" type=\"image/png\" href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\" />\n";
    cout << "<style>\n";
    cout << "* {\n";
    cout << "font-family: 'Source Code Pro', monospace;\n";
    cout << "font-size: 1rem !important;\n";
    cout << "}\n";
    cout << "body {\n";
    cout << "background-color: #212529;\n";
    cout << "}\n";
    cout << "pre {\n";
    cout << "color: #cccccc;\n";
    cout << "}\n";
    cout << "b {\n";
    cout << "color: #01b468;\n";
    cout << "}\n";
    cout << "</style>\n";
    cout << "</head>\n";
    cout << "<body>\n";
    cout << "<table class=\"table table-dark table-bordered\">\n";
    cout << "<thead>\n";
    cout << "<tr>\n";
    for (int i = 0; i < session_cnt; i++)
        if (strlen(host[i]))
            cout << "<th scope=\"col\">" << host[i] << ":" << port[i] << "</th>\n";
    cout << "</tr>\n";
    cout << "</thead>\n";
    cout << "<tbody>\n";
    cout << "<tr>\n";
    for (int i = 0; i < session_cnt; i++)
        if (strlen(host[i]))
        {
            char num[3];
            sprintf(num, "%d", i);
            cout << "<td><pre id=\"" << num <<"\" class=\"mb-0\"></pre></td>\n";
        }
    cout << "</tr>\n";
    cout << "</tbody>\n";
    cout << "</table>\n";
    cout << "</body>\n";
    cout << "</html>\n";
    cout << "\n";


    for(int i = 0; i < s.size(); i++)
        make_shared<Console>(move(s[i]))->start();
    ioc.run();

    return 0;
}
