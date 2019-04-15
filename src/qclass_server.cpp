// Copyright 2019 Qwant Research. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "katanoisi/qclass_server.h"
#include "katanoisi/utils.h"

qclass_server::qclass_server(Address addr,
    std::string& classif_config,
    int debug)
{
    httpEndpoint = std::make_shared<Http::Endpoint>(addr);
    _debug_mode = debug;

    std::ifstream model_config;
    model_config.open(classif_config);
    std::string line;

    while (getline(model_config, line)) {
        // @Christophe: this excludes every inline comment
        // WIP
        if (*line.begin() != '#') {
            std::vector<std::string> vecline;
            Split(line, vecline, "\t");
            string domain = vecline[0];
            string file = vecline[1];
            int online = 0;
            if ((int)vecline.size() > 2) {
                online = atoi(vecline[2].c_str());
            }
            cerr << domain << "\t" << file << "\t" << online << endl;
            _list_classifs.push_back(new qclassifier(file, domain));
        }
    }
    model_config.close();
}

void qclass_server::init(size_t thr)
{
    auto opts = Http::Endpoint::options().threads(thr).flags(
        Tcp::Options::InstallSignalHandler);
    httpEndpoint->init(opts);
    setupRoutes();
}

void qclass_server::start()
{
    httpEndpoint->setHandler(router.handler());
    httpEndpoint->serve();
    httpEndpoint->shutdown();
}

void qclass_server::setupRoutes()
{
    using namespace Rest;

    Routes::Post(router,
        "/intention/",
        Routes::bind(&qclass_server::doClassificationPost, this));

    Routes::Get(router,
        "/intention/",
        Routes::bind(&qclass_server::doClassificationGet, this));
}

void qclass_server::doClassificationGet(const Rest::Request& request,
    Http::ResponseWriter response)
{
    response.headers().add<Http::Header::AccessControlAllowHeaders>(
        "Content-Type");
    response.headers().add<Http::Header::AccessControlAllowMethods>(
        "GET, POST, DELETE, OPTIONS");
    response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
    response.headers().add<Http::Header::ContentType>(MIME(Application, Json));
    string response_string = "{\"domains\":[";
    for (int inc = 0; inc < (int)_list_classifs.size(); inc++) {
        if (inc > 0)
            response_string.append(",");
        response_string.append("\"");
        response_string.append(_list_classifs.at(inc)->getDomain());
        response_string.append("\"");
    }
    response_string.append("]}");
    if (_debug_mode != 0)
        cerr << "LOG: " << currentDateTime() << "\t" << response_string << endl;
    response.send(Pistache::Http::Code::Ok, response_string);
}

void qclass_server::doClassificationPost(const Rest::Request& request,
    Http::ResponseWriter response)
{
    response.headers().add<Http::Header::AccessControlAllowHeaders>(
        "Content-Type");
    response.headers().add<Http::Header::AccessControlAllowMethods>(
        "GET, POST, DELETE, OPTIONS");
    response.headers().add<Http::Header::AccessControlAllowOrigin>("*");
    nlohmann::json j = nlohmann::json::parse(request.body());
    int count = 10;
    bool debugmode = false;
    if (j.find("count") != j.end()) {
        count = j["count"];
    }
    if (j.find("debug") != j.end()) {
        debugmode = j["debug"];
    }
    if (j.find("language") == j.end()) {
        response.headers().add<Http::Header::ContentType>(MIME(Application, Json));
        response.send(Http::Code::Bad_Request,
            std::string("the language value is null"));
    }
    if (j.find("text") != j.end()) {
        string text = j["text"];
        string lang = j["language"];
        if (text.length() > 0) {
            qtokenizer l_tok(lang, true);
            j.push_back(nlohmann::json::object_t::value_type(
                string("tokenized"), l_tok.tokenize_str(text)));
            if (_debug_mode != 0)
                cerr << "LOG: " << currentDateTime() << "\t"
                     << "ASK CLASS :\t" << j << endl;
            if (j.find("domain") != j.end()) {
                string domain = j["domain"];
                string tokenized = j["tokenized"];
                std::vector<std::pair<fasttext::real, std::string>> results;
                results = askClassification(tokenized, domain, count);
                j.push_back(
                    nlohmann::json::object_t::value_type(string("intention"), results));
            } else {
                response.headers().add<Http::Header::ContentType>(
                    MIME(Application, Json));
                response.send(Http::Code::Bad_Request,
                    std::string("Domain value is null"));
            }
            std::string s = j.dump();
            if (_debug_mode != 0)
                cerr << "LOG: " << currentDateTime() << "\t" << s << endl;
            response.headers().add<Http::Header::ContentType>(
                MIME(Application, Json));
            response.send(Http::Code::Ok, std::string(s));
        } else {
            response.headers().add<Http::Header::ContentType>(
                MIME(Application, Json));
            response.send(Http::Code::Bad_Request,
                std::string("Length of text value is null"));
        }
    } else {
        response.headers().add<Http::Header::ContentType>(MIME(Application, Json));
        response.send(Http::Code::Bad_Request,
            std::string("the text value is null"));
    }
}

std::vector<std::pair<fasttext::real, std::string>>
qclass_server::askClassification(std::string& text,
    std::string& domain,
    int count)
{
    std::vector<std::pair<fasttext::real, std::string>> to_return;
    if ((int)text.size() > 0) {
        auto it_classif = std::find_if(
            _list_classifs.begin(),
            _list_classifs.end(),
            [&](qclassifier* l_classif) { return l_classif->getDomain() == domain; });
        if (it_classif != _list_classifs.end()) {
            to_return = (*it_classif)->prediction(text, count);
        }
    }
    return to_return;
}
bool qclass_server::process_localization(string& input, json& output)
{
    string token(input.c_str());
    if (input.find("à ") == 0)
        token = input.substr(3);
    if (input.find("au dessus de ") == 0)
        token = input.substr(13);
    if (input.find("au ") == 0)
        token = input.substr(4);
    if (input.find("vers ") == 0)
        token = input.substr(5);
    output.push_back(
        nlohmann::json::object_t::value_type(string("label"), token));
}

void qclass_server::doAuth(const Rest::Request& request,
    Http::ResponseWriter response)
{
    printCookies(request);
    response.cookies().add(Http::Cookie("lang", "fr-FR"));
    response.send(Http::Code::Ok);
}
