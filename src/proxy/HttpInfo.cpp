#include "../../proxy/HttpInfo.hpp"

using namespace nlohmann;
using namespace geode::prelude;

static size_t globalIndexCounter = 1;

proxy::converters::FormToJson proxy::HttpInfo::formToJson;

proxy::converters::RobTopToJson proxy::HttpInfo::robtopToJson;

proxy::converters::BinaryToRaw proxy::HttpInfo::binaryToRaw;

proxy::HttpInfo::HttpContent proxy::HttpInfo::getContent(const bool raw, const ContentType originalContentType, const std::string& path, const std::string& original) {
    if (raw) {
        return { originalContentType, original };
    } else switch (originalContentType) {
        case ContentType::JSON: return {
            ContentType::JSON,
            json::parse(original).dump(2)
        };
        case ContentType::FORM: return {
            ContentType::JSON,
            HttpInfo::formToJson.convert(path, original).dump(2)
        };
        case ContentType::ROBTOP: return {
            ContentType::JSON,
            HttpInfo::robtopToJson.convert(path, original).dump(2)
        };
        case ContentType::BINARY: return {
            ContentType::BINARY,
            HttpInfo::binaryToRaw.convert(path, original)
        };
        default: return { originalContentType, original };
    }
}

proxy::HttpInfo::ContentType proxy::HttpInfo::determineContentType(const std::string& path, const std::string& content, const bool isBody) {
    if (content.empty()) {
        return ContentType::UNKNOWN_CONTENT;
    } else if (HttpInfo::binaryToRaw.canConvert(path, content)) {
        return ContentType::BINARY;
    } else if (converters::isJson(content)) {
        return ContentType::JSON;
    } else if (content.starts_with('<')) {
        return ContentType::XML;
    } else if (!isBody && HttpInfo::robtopToJson.canConvert(path, content)) {
        return ContentType::ROBTOP;
    } else if (HttpInfo::formToJson.canConvert(path, content)) {
        return ContentType::FORM;
    } else {
        return ContentType::UNKNOWN_CONTENT;
    }
}

nlohmann::json proxy::HttpInfo::parseCocosHeaders(const gd::vector<char>* headers) {
    std::stringstream headerStream(std::string(headers->begin(), headers->end()));
    std::vector<gd::string> headerStrings;

    for (std::string header; std::getline(headerStream, header, '\n');) {
        headerStrings.push_back(header);
    }

    return HttpInfo::parseCocosHeaders(headerStrings);
}

nlohmann::json proxy::HttpInfo::parseCocosHeaders(const gd::vector<gd::string>& headers) {
    json parsed = json::object();

    // CCHttp headers technically allow for weird header formats, but I'm assuming they're all key-value pairs separated by a colon since this is the standard
    // If you don't follow the standard, I'm not going to care that you get shitty results
    for (const gd::string& header : headers) {
        const std::string headerString(header.c_str());
        const size_t colonPos = headerString.find(":");

        if (colonPos == std::string::npos) {
            parsed[headerString] = json();
        } else {
            std::string value(headerString.substr(colonPos + 1));

            parsed[headerString.substr(0, colonPos)] = json(value.erase(0, value.find_first_not_of(' ')));
        }
    }

    return parsed;
}

bool proxy::HttpInfo::shouldPause() {
    return Mod::get()->getSettingValue<bool>("pause-requests");
}

proxy::HttpInfo::HttpInfo(CCHttpRequest* request) : m_id(globalIndexCounter++),
m_paused(this->shouldPause()),
m_request(request) { }

proxy::HttpInfo::HttpInfo(web::WebRequest* request, const std::string& method, const std::string& url) : m_id(globalIndexCounter++),
m_paused(this->shouldPause()),
m_request(request, method, url) { }

bool proxy::HttpInfo::isPaused() const {
    return m_paused;
}

bool proxy::HttpInfo::responseReceived() const {
    return m_response.received();
}

void proxy::HttpInfo::resume() {
    m_paused = false;
}

std::string proxy::HttpInfo::URL::stringifyMethod(const CCHttpRequest::HttpRequestType method) {
    switch (method) {
        case CCHttpRequest::kHttpGet: return "GET";
        case CCHttpRequest::kHttpPost: return "POST";
        case CCHttpRequest::kHttpPut: return "PUT";
        case CCHttpRequest::kHttpDelete: return "DELETE";
        default: return "UNKNOWN";
    }
}

proxy::HttpInfo::Origin proxy::HttpInfo::URL::determineOrigin(const std::string& host) {
    #define isDomain(domain) host == std::string(domain) || host.ends_with("." + std::string(domain))

    if (isDomain("boomlings.com") || isDomain("geometrydash.com")) {
        return Origin::GD;
    } else if (isDomain("geometrydashfiles.b-cdn.net") || isDomain("geometrydashcontent.b-cdn.net")) {
        return Origin::GD_CDN;
    // robtop.games is currently in the process of being sold
    } else if (
        isDomain("robtopgames.org") ||
        isDomain("robtopgames.net") ||
        isDomain("robtopgames.com") ||
        isDomain("robtop.games")
    ) {
        return Origin::ROBTOP_GAMES;
    } else if (isDomain("ngfiles.com")) {
        return Origin::NEWGROUNDS;
    } else if (isDomain("geode-sdk.org")) {
        return Origin::GEODE;
    } else if (isDomain("localhost")) {
        return Origin::LOCALHOST;
    } else {
        return Origin::OTHER;
    }
}

proxy::HttpInfo::URL::URL(CCHttpRequest* request) : m_method(URL::stringifyMethod(request->getRequestType())),
m_original(request->getUrl()),
m_query(json::object()) {
    this->parse();
}

proxy::HttpInfo::URL::URL(web::WebRequest* request, const std::string& method, const std::string& url) : m_method(method),
m_original(url),
m_query(json::object()) {
    const std::unordered_map<std::string, std::string> params = request->getUrlParams();

    if (params.size() && m_original.find('?') == std::string::npos) {
        m_original += "?";
    }

    for (const auto& [key, value] : params) {
        m_original += key + '=' + value + '&';
    }

    if (m_original.ends_with('&')) {
        m_original.pop_back();
    }

    this->parse();
}

std::string proxy::HttpInfo::URL::stringifyProtocol() const {
    switch (m_protocol) {
        case Protocol::HTTP: return "HTTP";
        case Protocol::HTTPS: return "HTTPS";
        default: return "UNKNOWN";
    }
}

std::string proxy::HttpInfo::URL::stringifyQuery() const {
    return m_query.dump(2);
}

std::string proxy::HttpInfo::URL::getPortHost() const {
    if (m_origin != LOCALHOST && ((m_port == 80 && m_protocol == Protocol::HTTP) || (m_port == 443 && m_protocol == Protocol::HTTPS))) {
        return m_host;
    } else {
        return m_host + ":" + std::to_string(m_port);
    }
}

void proxy::HttpInfo::URL::parse() {
    const size_t protocolEnd = m_original.find("://");
    const size_t queryStart = m_original.find('?');
    std::string rawHost;
    size_t pathStart;
    size_t portStart;

    if (protocolEnd == std::string::npos) {
        pathStart = m_original.find('/');
        m_protocol = Protocol::UNKNOWN_PROTOCOL;
        rawHost = m_original.substr(0, pathStart == std::string::npos ? queryStart : pathStart);
    } else {
        const std::string protocol(m_original.substr(0, protocolEnd));

        if (protocol == "https") {
            m_protocol = Protocol::HTTPS;
        } else if (protocol == "http") {
            m_protocol = Protocol::HTTP;
        } else {
            m_protocol = Protocol::UNKNOWN_PROTOCOL;
        }

        pathStart = m_original.find('/', protocolEnd + 3);
        rawHost = m_original.substr(protocolEnd + 3, (pathStart == std::string::npos ? queryStart : pathStart) - protocolEnd - 3);
    }

    if (pathStart == std::string::npos) {
        m_path = "/";
    } else {
        m_path = m_original.substr(pathStart, queryStart == std::string::npos ? std::string::npos : queryStart - pathStart);
    }

    if (queryStart != std::string::npos) {
        m_query = HttpInfo::formToJson.convert(m_path, m_original.substr(queryStart + 1));
    }

    portStart = rawHost.find(':');
    m_host = rawHost.substr(0, portStart);
    m_origin = URL::determineOrigin(m_host);
    m_queryLess = m_original.substr(0, queryStart);

    if (portStart == std::string::npos) {
        m_port = m_protocol == Protocol::HTTPS ? 443 : 80;
    } else {
        m_port = std::stoi(rawHost.substr(portStart + 1));
    }
}

proxy::HttpInfo::Request::Request(CCHttpRequest* request) : m_url(request), 
m_headers(HttpInfo::parseCocosHeaders(request->getHeaders())),
m_body(std::string(request->getRequestData(), request->getRequestDataSize())),
m_contentType(HttpInfo::determineContentType(m_url.getPath(), m_body, true)) { }

proxy::HttpInfo::Request::Request(web::WebRequest* request, const std::string& method, const std::string& url) : m_url(request, method, url),
m_headers(json::object()) {
    const ByteVector body = request->getBody().value_or(ByteVector());

    for (const auto& [key, value] : request->getHeaders()) {
        m_headers[key] = json(value);
    }

    m_body = std::string(body.begin(), body.end());
    m_contentType = HttpInfo::determineContentType(m_url.getPath(), m_body, true);
}

std::string proxy::HttpInfo::Request::stringifyHeaders() const {
    return m_headers.dump(2);
}

proxy::HttpInfo::HttpContent proxy::HttpInfo::Request::getBodyContent(const bool raw) const {
    return HttpInfo::getContent(raw, m_contentType, m_url.getPath(), m_body);
}

proxy::HttpInfo::Response::Response() : m_statusCode(0), m_received(false) { };

proxy::HttpInfo::Response::Response(Request* request, CCHttpResponse* response) : m_received(true),
m_request(request),
m_headers(HttpInfo::parseCocosHeaders(response->getResponseHeader())),
m_statusCode(response->getResponseCode()) {
    gd::vector<char>* data = response->getResponseData();

    m_response = std::string(data->begin(), data->end());
    m_contentType = HttpInfo::determineContentType(request->m_url.getPath(), m_response);
}

proxy::HttpInfo::Response::Response(Request* request, web::WebResponse* response) : m_received(true),
m_request(request),
m_headers(json::object()),
m_statusCode(response->code()),
m_response(response->string().unwrapOrDefault()),
m_contentType(HttpInfo::determineContentType(request->m_url.getPath(), m_response)) {
    for (const std::string& key : response->headers()) {
        m_headers[key] = json(response->header(key).value_or(""));
    }
}

std::string proxy::HttpInfo::Response::stringifyHeaders() const {
    return m_headers.dump(2);
}

std::string proxy::HttpInfo::Response::stringifyStatusCode() const {
    switch (m_statusCode) {
        case -3: return "Request Cancelled";
        case -2: return "Request Timeout";
        case -1: return "Request Error";
        case 0: return "No response available yet";
        default: return std::to_string(m_statusCode);
    }
}

proxy::HttpInfo::HttpContent proxy::HttpInfo::Response::getResponseContent(const bool raw) const {
    return HttpInfo::getContent(raw, m_contentType, m_request->getURL().getPath(), m_response);
}

bool proxy::HttpInfo::Response::received() const {
    return m_received;
}