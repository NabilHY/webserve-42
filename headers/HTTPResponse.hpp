#pragma once

#include <map>
#include <string>
#include <sstream>
#include <filesystem>
#include "FileStreamer.hpp"
#include "HTTPRequest.hpp"
#include "requestUtils.hpp"
#include "responseUtils.hpp"

class FileStreamer;
class HTTPRequest;

class HTTPResponse {
public:
    // Constructors
    HTTPResponse(HTTPRequest* request, std::string cookies);
    ~HTTPResponse();

    std::string getNextChunk();
    bool isComplete() const;
    std::string getConnectionHeader();

    size_t  getFileSize(std::string path) const;
    void    setStatus(int code, const std::string& message);
    void    setHeader(const std::string& key, const std::string& value);
    std::map<std::string, std::string> getHeaders() const;
    void    setConnection(const std::string& connection);

    void    setBody(const std::string& body);              
    void    setStreamer(FileStreamer* streamer);           

    int     getStatusCode() const;
    std::string getStatusMessage() const;
    void buildAutoIndexResponse(HTTPRequest*);

    static std::string getMimeType(const std::string& path);

    bool hasCookie();

    std::string getBody() const {
        return _body;
    }

private:
    int                             _statusCode;
    std::string                     _statusMessage;
    std::string                     _connectionType;
    std::string                     _connection;

    FileStreamer*                   _streamer;
    HTTPRequest*                    _request;
    std::string                     _body;
    std::string                     _tempFilePath;
    size_t                          _bodyPos;

    std::map<std::string, std::string> _headers;

    bool                            _headersSent;
    bool                            _complete;
    bool                            _hasCookie;

    static std::map<std::string, std::string> _mimeTypes;

    bool hasCookie() const { return _hasCookie; }
};

