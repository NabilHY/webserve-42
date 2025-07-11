#include "../../headers/Connection.hpp"
#include "../../headers/Multiplexer.hpp"
#include "../../headers/HTTPRequest.hpp"
#include "../../headers/FileStreamer.hpp"

Connection::Connection()
    : _fd(-1),
      _epoll_fd(-1),
      _readBuffer(),
      _writeBuffer(),
      _connectionHeader(""),
      _headersPart(""),
      _closed(false),
      _config(NULL),
      _streamer(NULL),
      _httpResponse(NULL),
      _httpRequest(NULL),
      _headersParsed(false),
      _expectedBodyLength(0),
      _completedBuffer(false),
      _serverClusterId(0),
      _hasCookie(false),
      _cookieHeader("")
{}

Connection::Connection(int fd, int epoll_fd, WebServerConfig* config, int serverClusterId)
    : _fd(fd),_epoll_fd(epoll_fd),_readBuffer(),_writeBuffer(),_connectionHeader(""),
    _headersPart(""),_closed(false),_config(config),_streamer(NULL),_httpResponse(NULL),
    _httpRequest(NULL),_headersParsed(false),_expectedBodyLength(0),_completedBuffer(false),
    _serverClusterId(serverClusterId),_hasCookie(false),_cookieHeader("")
{}

// std::string& Connection::getReadBuffer() {
//     return this->_readBuffer;
// }

// ! leaks
Connection::~Connection () {}

std::string& Connection::getWriteBuffer() {
    return this->_writeBuffer;
}

std::vector<char>& Connection::getReadBuffer() {
    return this->_readBuffer;
}

void Connection::parseContentLength() {
    if (_headersPart.find("POST") == std::string::npos)
        return;

    size_t start = _headersPart.find("Content-Length:");
    if (start == std::string::npos)
        return;
    start += std::string("Content-Length:").size();
    size_t end = _headersPart.find("\r\n", start);
    std::string value = _headersPart.substr(start, end - start);
    _expectedBodyLength = std::atol(value.c_str());
}

std::string replaceBgColorUrl(const std::string& setCookieHeader, const std::string& newUrl) {
    const std::string key = "bgColor=";
    size_t keyPos = setCookieHeader.find(key);
    if (keyPos == std::string::npos)
        return setCookieHeader; // bgColor not found, return unchanged

    size_t valueStart = keyPos + key.length();
    size_t valueEnd = setCookieHeader.find(";", valueStart);
    if (valueEnd == std::string::npos)
        valueEnd = setCookieHeader.length(); // no semicolon found, assume end of string

    // Reconstruct the string with the new URL
    std::string updated = setCookieHeader.substr(0, valueStart);
    updated += newUrl;
    updated += setCookieHeader.substr(valueEnd);

    return updated;
}

void Connection::parseCookie() {
    const std::string bgCookieName = "bgColor";
    const std::string _serverBgColorValue = "https://i.ibb.co/Gf7Nnfk2/team.png"; // define locally

    size_t cookieStart = _headersPart.find("Cookie:");
    std::string clientValue;

    if (cookieStart != std::string::npos) {
        size_t lineEnd = _headersPart.find("\r\n", cookieStart);
        std::string cookieLine = _headersPart.substr(cookieStart + 7, lineEnd - (cookieStart + 7));

        size_t bgPos = cookieLine.find("bgColor=");
        if (bgPos != std::string::npos) {
            size_t valueStart = bgPos + 8;
            size_t valueEnd = cookieLine.find(";", valueStart);
            clientValue = (valueEnd != std::string::npos)
                          ? cookieLine.substr(valueStart, valueEnd - valueStart)
                          : cookieLine.substr(valueStart);
        }
    }

    if (clientValue.empty()) {
        // Client has no cookie at all → set default
        Cookie newCookie(bgCookieName, _serverBgColorValue, 86400);
        _cookieHeader = newCookie.getHeader();
    } else if (clientValue != _serverBgColorValue) {
        // Client sent a different value → accept it, but update the expiration
        Cookie newCookie(bgCookieName, clientValue, 86400);
        _cookieHeader = newCookie.getHeader();
    } else {
        // Value matches → do not resend Set-Cookie
        _cookieHeader = "";
    }
}

void Connection::switchToWrite() {
    _httpRequest = new HTTPRequest(); // builds a 413 response
    _httpResponse = new HTTPResponse(_httpRequest, _cookieHeader);

    _closed = true; // close after writing 413
    _writeBuffer = _httpResponse->getNextChunk();

    struct epoll_event ev;
    ev.data.fd = _fd;
    ev.events = EPOLLOUT | EPOLLRDHUP;
    if (epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, _fd, &ev) == -1) {
        std::cerr << "epoll_ctl: switch to EPOLLOUT (Payload Too Large)\n";
        _closed = true;
    }
}

void Connection::printReqRes(HTTPRequest *req, HTTPResponse *res) {
    std::cout << "\n\033[1;31m******************************\033[0m\n";            
    std::cout << "\033[1;32m== HTTP REQUEST ==\n" << _headersPart << "\033[0m\n";         
    printResponse(res, req);
    std::cout << "\033[1;31m******************************\033[0m\n\n";

}


void Connection::handleRead() {
    char buffer[4096] = {0};

    ssize_t bytes_read;

    bytes_read = recv(_fd, buffer, sizeof(buffer), 0);

    if (bytes_read > 0) {
        // Append new data to the read buffer
        _readBuffer.insert(_readBuffer.end(), buffer, buffer + bytes_read);

        // If headers haven't been parsed yet, look for the delimiter
            const std::string delimiter = "\r\n\r\n";
            std::vector<char>::iterator it = std::search(
                _readBuffer.begin(),
                _readBuffer.end(),
                delimiter.begin(),
                delimiter.end()
            );

            // std::string reqRaw = std::string(_readBuffer.begin(), _readBuffer.end());

            if (it != _readBuffer.end()) {
                size_t pos = std::distance(_readBuffer.begin(), it);
                _headersPart = std::string(_readBuffer.begin(), _readBuffer.begin() + pos + delimiter.size());
                _headersParsed = true;
                parseContentLength();
            }

            
            if (_headersPart.find("Cookie:") != std::string::npos) {
                _hasCookie = true;
                parseCookie();
            }

        if (_headersParsed) {
            if (_expectedBodyLength > _config->getMaxBodySize()) {
                switchToWrite();
                printReqRes(_httpRequest, _httpResponse);
                return ;
            }

            if ( _expectedBodyLength == 0 || (_readBuffer.size() >= (_headersPart.size() + _expectedBodyLength))) {
                _httpRequest = new HTTPRequest(_readBuffer, _config, _serverClusterId);
                _httpResponse = new HTTPResponse(_httpRequest, _cookieHeader);
                re_armFd();
            } else {
                return;
            }
            printReqRes(_httpRequest, _httpResponse);
            return ;
        }
    } else {
        _closed = true;
        throw Multiplexer::ClientDisconnectedException();
    }
    re_armFd();
}

void Connection::re_armFd() {
    struct epoll_event ev;
    ev.data.fd = _fd;

    if (_closed) {
        ev.events = EPOLLHUP | EPOLLRDHUP;
    } else if (!_writeBuffer.empty() || (_httpResponse && !_httpResponse->isComplete())) {
        ev.events = EPOLLOUT | EPOLLRDHUP;
    } else {
        ev.events = EPOLLIN | EPOLLRDHUP;
    }

    if (epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, _fd, &ev) == -1) {
        std::cerr << "epoll_ctl: re_armFd" << std::endl;
        _closed = true;
    }
}

void Connection::handleWrite() {
    if (_httpResponse && _httpResponse->isComplete() && _writeBuffer.empty()) {
        if (_closed) {
            close(_fd);
            return;
        }
        reset();
        re_armFd();
        return;
    }

    if (_httpResponse && !_httpResponse->isComplete()) {
        std::string chunk = _httpResponse->getNextChunk();
        if (!chunk.empty())
            _writeBuffer.append(chunk);
    }

    if (!_writeBuffer.empty()) {
        ssize_t bytesSent = send(_fd, _writeBuffer.c_str(), _writeBuffer.size(), 0);

        if (bytesSent > 0)
            _writeBuffer.erase(0, bytesSent);
        else if (bytesSent == -1) {
            _closed = true;
            throw Multiplexer::ClientDisconnectedException();
        }
    }

    if ((!_writeBuffer.empty() || (_httpResponse && !_httpResponse->isComplete())) && !_closed)
        re_armFd();
}


// ! leaks
void	Connection::reset() {

	_writeBuffer.clear();
	_readBuffer.clear();
    if (_streamer) {
        delete _streamer;
        _streamer = NULL;
    }
    if (_httpResponse) {
        delete _httpResponse;
        _httpResponse = NULL;
    }
    if (_httpRequest != NULL) {
        delete _httpRequest;
        _httpRequest = NULL;
    }
}

bool	Connection::isClosed()	const {
    return _closed;
}