#include <iostream>
#include "ZatData.h"
#include <sstream>
#include <regex>
#include "../lib/tinyxml2/tinyxml2.h"
#include <json/json.h>
#include "HTTPSocket.h"
#include "platform/sockets/tcp.h"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
 #pragma comment(lib, "ws2_32.lib")
  #include <stdio.h>
 #include <stdlib.h>
#endif


#define DEBUG

#ifdef DEBUG
#define D(x) x
#else
#define D(x)
#endif

using namespace ADDON;
using namespace std;

void ZatData::sendHello() {
    ostringstream dataStream;
    dataStream << "uuid=888b4f54-c127-11e5-9912-ba0be0483c18&lang=en&format=json&client_app_token=" << appToken;

    HTTPSocketRaw *socket = new HTTPSocketRaw();
    Request request;
    request.url = helloUrl;
    request.method = POST;
    request.body = dataStream.str();
    request.AddHeader("Cookie", cookie);
    Response response;
    socket->Execute(request, response);
    cookie = response.cookie;

    //httpResponse resp = postRequest("/zapi/session/hello", data);
}

bool ZatData::login() {
    ostringstream dataStream;
    dataStream << "login=" << username << "&password=" << password << "&format=json";

    HTTPSocketRaw *socket = new HTTPSocketRaw();
    Request request;
    request.url = loginUrl;
    request.method = POST;
    request.body = dataStream.str();
    request.AddHeader("Cookie", cookie);
    Response response;
    socket->Execute(request, response);
    cookie = response.cookie;
    std::string jsonString = response.body;

    Json::Value json;
    Json::Reader reader;
    bool parsingSuccessful = reader.parse(jsonString,json);

    if (!parsingSuccessful){
        // report to the user the failure and their locations in the document.
        D(cout  << "Failed to parse login\n" << reader.getFormatedErrorMessages());
        return false;
    }

    string succ = json["success"].asString();
    if(succ == "False") {
        return false;
    }
    powerHash = json["account"]["power_guide_hash"].asString();
    D(cout << "Power Hash: " << powerHash << endl);
    return true;
}

void ZatData::loadAppId() {
    HTTPSocket *httpSocket = new HTTPSocketRaw();
    Request request;
    request.url = appIdUrl;
    Response response;
    httpSocket->Execute(request, response);

    appToken = "";

    std::string html = response.body;

    std::smatch m;
    std::regex e ("appToken.*\\'(.*)\\'");

    std::string token = "";

    if (std::regex_search(html, m, e)) {
        token = m[1];
    }

    cout << token << endl;

    appToken = token;

    XBMC->Log(LOG_DEBUG, "Loaded App token %s", XBMC->UnknownToUTF8(appToken.c_str()));
}

Json::Value ZatData::loadFavourites() {
    ostringstream urlStream;
    urlStream << favoritesUrl;

    HTTPSocketRaw *socket = new HTTPSocketRaw();
    Request request;
    request.url = urlStream.str();
    request.AddHeader("Cookie", cookie);
    Response response;
    socket->Execute(request, response);
    cookie = response.cookie;
    std::string jsonString = response.body;

    cout << jsonString << endl;
    Json::Value json;
    Json::Reader reader;

    reader.parse(jsonString,json);
    return json["favorites"];
}

void ZatData::loadChannels() {
    Json::Value favs = loadFavourites();

    ostringstream urlStream;
    urlStream << channelsUrl << powerHash << "?details=False";

    HTTPSocketRaw *socket = new HTTPSocketRaw();
    Request request;
    request.url = urlStream.str();
    request.AddHeader("Cookie", cookie);
    Response response;
    socket->Execute(request, response);
    cookie = response.cookie;
    std::string jsonString = response.body;

    Json::Value json;
    Json::Reader reader;
    
    if (!reader.parse(jsonString,json)){
        // report to the user the failure and their locations in the document.
        std::cout  << "Failed to parse configuration\n"
        << reader.getFormatedErrorMessages();
        return;
    }

    channelNumber = 1;
    Json::Value groups = json["channel_groups"];

    PVRZattooChannelGroup favGroup;
    favGroup.name = "Favoriten";
    //Load the channel groups and channels
    for (Json::Value::ArrayIndex index = 0; index < groups.size(); ++index) {
        PVRZattooChannelGroup group;
        group.name = groups[index]["name"].asString();
        Json::Value channels = groups[index]["channels"];
        for(Json::Value::ArrayIndex i = 0; i < channels.size(); ++i) {
            Json::Value qualities = channels[i]["qualities"];
            for(Json::Value::ArrayIndex q = 0; q < qualities.size(); ++q) {
                if(qualities[q]["availability"].asString() == "available") {
                    ZatChannel channel;
                    channel.name = qualities[q]["title"].asString();
                    channel.strStreamURL = "";
                    //cout << channel.name << endl;
                    std::string cid = channels[i]["cid"].asString(); //returns std::size_t
                    channel.iUniqueId = GetChannelId(cid.c_str());
                    channel.cid = cid;
                    channel.iChannelNumber = ++channelNumber;
                    channel.strLogoPath = "http://logos.zattic.com";
                    channel.strLogoPath.append(qualities[q]["logo_white_84"].asString());
                    group.channels.insert(group.channels.end(), channel);
                    //Yeah thats bad performance
                    for (Json::Value::ArrayIndex fav = 0; fav < favs.size(); fav++) {
                        if (favs[fav].asString() == cid) {
                            favGroup.channels.insert(favGroup.channels.end(), channel);
                        }
                    }
                    break;
                }   
            }
        }
        if (group.channels.size() > 0)
            channelGroups.insert(channelGroups.end(),group);
    }

    if (favGroup.channels.size() > 0)
        channelGroups.insert(channelGroups.end(),favGroup);
}

int ZatData::GetChannelId(const char * strChannelName)
{
    int iId = 0;
    int c;
    while (c = *strChannelName++)
        iId = ((iId << 5) + iId) + c; /* iId * 33 + c */
    return abs(iId);
}

int ZatData::GetChannelGroupsAmount() {
    return channelGroups.size();
}

ZatData::ZatData(ZatProvider prov, std::string u, std::string p)  {
	provider = prov;
    username = u;
    password = p;
    m_iLastStart    = 0;
    m_iLastEnd      = 0;
    cookie = "";	
	
	baseUrl = "zattoo.com";
	if (Quickline == prov)
	{
		baseUrl = "mobiltv.quickline.com";
	}
	helloUrl = baseUrl + "/zapi/session/hello";
    loginUrl = baseUrl + "/zapi/account/login";
    appIdUrl = baseUrl + "/login";
    favoritesUrl = baseUrl + "/zapi/channels/favorites";
    channelsUrl = baseUrl + "/zapi/v2/cached/channels/";
    watchUrl = baseUrl + "/zapi/watch";
    epgUrl = baseUrl + "/zapi/v2/cached/program/power_guide/";

    cookiePath = GetUserFilePath("zatCookie.txt");	

    this->loadAppId();
    this->sendHello();
    if(this->login()) {
        this->loadChannels();
    }
    else {
        XBMC->QueueNotification(QUEUE_ERROR, "Zattoo Login fehlgeschlagen!");
    }
}

ZatData::~ZatData() {
    channelGroups.clear();
    
}

void *ZatData::Process(void) {
    return NULL;
}

PVR_ERROR ZatData::GetChannelGroups(ADDON_HANDLE handle) {
    std::vector<PVRZattooChannelGroup>::iterator it;
    for (it = channelGroups.begin(); it != channelGroups.end(); ++it)
    {
            PVR_CHANNEL_GROUP xbmcGroup;
            memset(&xbmcGroup, 0, sizeof(PVR_CHANNEL_GROUP));
            xbmcGroup.iPosition = 0;      /* not supported  */
            xbmcGroup.bIsRadio  = false; /* is radio group */
            strncpy(xbmcGroup.strGroupName, it->name.c_str(), sizeof(xbmcGroup.strGroupName) - 1);

            PVR->TransferChannelGroup(handle, &xbmcGroup);
    }
    return PVR_ERROR_NO_ERROR;
}

PVRZattooChannelGroup *ZatData::FindGroup(const std::string &strName) {
    std::vector<PVRZattooChannelGroup>::iterator it;
    for(it = channelGroups.begin(); it < channelGroups.end(); ++it)
    {
        if (it->name == strName)
            return &*it;
    }

    return NULL;
}

PVR_ERROR ZatData::GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group) {
    PVRZattooChannelGroup *myGroup;
    if ((myGroup = FindGroup(group.strGroupName)) != NULL)
    {
        std::vector<ZatChannel>::iterator it;
        for (it = myGroup->channels.begin(); it != myGroup->channels.end(); ++it)
        {
            ZatChannel &channel = (*it);
            PVR_CHANNEL_GROUP_MEMBER xbmcGroupMember;
            memset(&xbmcGroupMember, 0, sizeof(PVR_CHANNEL_GROUP_MEMBER));

            strncpy(xbmcGroupMember.strGroupName, group.strGroupName, sizeof(xbmcGroupMember.strGroupName) - 1);
            xbmcGroupMember.iChannelUniqueId = channel.iUniqueId;
            xbmcGroupMember.iChannelNumber   = channel.iChannelNumber;

            PVR->TransferChannelGroupMember(handle, &xbmcGroupMember);
        }
    }

    return PVR_ERROR_NO_ERROR;
}

int ZatData::GetChannelsAmount(void) {
    return channelNumber-1;
}

PVR_ERROR ZatData::GetChannels(ADDON_HANDLE handle, bool bRadio) {
    std::vector<PVRZattooChannelGroup>::iterator it;
    for (it = channelGroups.begin(); it != channelGroups.end(); ++it)
    {
        std::vector<ZatChannel>::iterator it2;
        for (it2 = it->channels.begin(); it2 != it->channels.end(); ++it2)
        {
            ZatChannel &channel = (*it2);

            PVR_CHANNEL kodiChannel;
            memset(&kodiChannel, 0, sizeof(PVR_CHANNEL));

            kodiChannel.iUniqueId         = channel.iUniqueId;
            kodiChannel.bIsRadio          = false;
            kodiChannel.iChannelNumber    = channel.iChannelNumber;
            strncpy(kodiChannel.strChannelName, channel.name.c_str(), sizeof(kodiChannel.strChannelName) - 1);
            kodiChannel.iEncryptionSystem = 0;

            strncpy(kodiChannel.strIconPath, channel.strLogoPath.c_str(), sizeof(kodiChannel.strIconPath) - 1);

            kodiChannel.bIsHidden         = false;
			
            // self referencing so GetLiveStreamURL() gets triggered
            std::string streamURL;
            streamURL = ("pvr://stream/tv/zattoo.ts");
            strncpy(kodiChannel.strStreamURL, streamURL.c_str(), sizeof(kodiChannel.strStreamURL) - 1);
            //

            PVR->TransferChannelEntry(handle, &kodiChannel);
        }
    }
    return PVR_ERROR_NO_ERROR;
}

std::string ZatData::GetChannelStreamUrl(int uniqueId) {
    ZatChannel *channel = FindChannel(uniqueId);
    //XBMC->QueueNotification(QUEUE_INFO, "Getting URL for channel %s", XBMC->UnknownToUTF8(channel->name.c_str()));

    ostringstream dataStream;
    dataStream << "cid=" << channel->cid << "&stream_type=hls&format=json";

    HTTPSocketRaw *socket = new HTTPSocketRaw();
    Request request;
    request.url = watchUrl;
    request.method = POST;
    request.body = dataStream.str();
    request.AddHeader("Cookie", cookie);
    Response response;
    socket->Execute(request, response);
    cookie = response.cookie;

    std::string jsonString = response.body;

    Json::Value json;
    Json::Reader reader;
    bool parsingSuccessful = reader.parse(jsonString,json);

    string url = json["stream"]["url"].asString();
    return url;
}

ZatChannel *ZatData::FindChannel(int uniqueId) {
    std::vector<PVRZattooChannelGroup>::iterator it;
    for (it = channelGroups.begin(); it != channelGroups.end(); ++it)
    {
        std::vector<ZatChannel>::iterator it2;
        for (it2 = it->channels.begin(); it2 != it->channels.end(); ++it2)
        {
            ZatChannel &channel = (*it2);
            if(channel.iUniqueId == uniqueId) {
                return &channel;
            }
        }
    }
    return NULL;
}

int ZatData::findChannelNumber(int uniqueId) {
    ZatChannel *channel = FindChannel(uniqueId);
    return 0;
}

PVR_ERROR ZatData::GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd) {
    ZatChannel *zatChannel = FindChannel(channel.iUniqueId);

    if (iStart > m_iLastStart || iEnd > m_iLastEnd)
    {
        // reload EPG for new time interval only
        LoadEPG(iStart, iEnd);
        {
            // doesn't matter is epg loaded or not we shouldn't try to load it for same interval
            m_iLastStart = iStart;
            m_iLastEnd = iEnd;
        }
    }

    std::vector<PVRIptvEpgEntry>::iterator it;
    for (it = zatChannel->epg.begin(); it != zatChannel->epg.end(); ++it)
    {
        PVRIptvEpgEntry &epgEntry = (*it);

        EPG_TAG tag;
        memset(&tag, 0, sizeof(EPG_TAG));

        tag.iUniqueBroadcastId  = epgEntry.iBroadcastId;
        tag.strTitle            = epgEntry.strTitle.c_str();
        tag.iChannelNumber      = epgEntry.iChannelId;
        tag.startTime           = epgEntry.startTime;
        tag.endTime             = epgEntry.endTime;
        tag.strPlotOutline      = epgEntry.strPlot.c_str();//epgEntry.strPlotOutline.c_str();
        tag.strPlot             = epgEntry.strPlot.c_str();
        tag.strOriginalTitle    = NULL;  /* not supported */
        tag.strCast             = NULL;  /* not supported */
        tag.strDirector         = NULL;  /*SA not supported */
        tag.strWriter           = NULL;  /* not supported */
        tag.iYear               = 0;     /* not supported */
        tag.strIMDBNumber       = NULL;  /* not supported */
        tag.strIconPath         = epgEntry.strIconPath.c_str();
//        if (FindEpgGenre(myTag->strGenreString, iGenreType, iGenreSubType))
//        {
//            tag.iGenreType          = iGenreType;
//            tag.iGenreSubType       = iGenreSubType;
//            tag.strGenreDescription = NULL;
//        }
//        else
//        {
            tag.iGenreType          = EPG_GENRE_USE_STRING;
            tag.iGenreSubType       = 0;     /* not supported */
            tag.strGenreDescription = epgEntry.strGenreString.c_str();
//        }
        tag.iParentalRating     = 0;     /* not supported */
        tag.iStarRating         = 0;     /* not supported */
        tag.bNotify             = false; /* not supported */
        tag.iSeriesNumber       = 0;     /* not supported */
        tag.iEpisodeNumber      = 0;     /* not supported */
        tag.iEpisodePartNumber  = 0;     /* not supported */
        tag.strEpisodeName      = NULL;  /* not supported */
        tag.iFlags              = EPG_TAG_FLAG_UNDEFINED;

        PVR->TransferEpgEntry(handle, &tag);
    }

    return PVR_ERROR_NO_ERROR;
}

bool ZatData::LoadEPG(time_t iStart, time_t iEnd) {
//    iStart -= (iStart % (3600/2)) - 86400; // Do s
//    iEnd = iStart + 3600*3;

    //Do some time magic that the start date is not to far in the past because zattoo doesnt like that
    time_t tempStart = iStart - (iStart % (3600/2)) - 86400;
    time_t tempEnd = tempStart + 3600*5; //Add 5 hours

    while(tempEnd < iEnd) {
        ostringstream urlStream;
        urlStream << epgUrl << powerHash << "?end=" << tempEnd << "&start=" << tempStart << "&format=json";

        HTTPSocket *socket = new HTTPSocketRaw();
        Request request;
        request.url = urlStream.str();
        request.AddHeader("Cookie", cookie);
        Response response;
        socket->Execute(request,response);
        cookie = response.cookie;

        std::string jsonString = response.body;

        Json::Value json;
        Json::Reader reader;
        bool parsingSuccessful = reader.parse(jsonString,json);

        //D(cout << json << endl);

        Json::Value channels = json["channels"];

        //Load the channel groups and channels
        for (Json::Value::ArrayIndex index = 0; index < channels.size(); ++index) {
            string cid = channels[index]["cid"].asString();
            for (Json::Value::ArrayIndex i = 0; i < channels[index]["programs"].size(); ++i) {
                Json::Value program = channels[index]["programs"][i];
                int channelId = GetChannelId(cid.c_str());
                ZatChannel *channel = FindChannel(channelId);

                if (!channel) {
                    continue;
                }

                PVRIptvEpgEntry entry;
                entry.strTitle = program["t"].asString();
                entry.startTime = program["s"].asInt();
                entry.endTime = program["e"].asInt();
                entry.iBroadcastId = program["id"].asInt();
                entry.strIconPath = program["i_url"].asString();
                entry.iChannelId = channel->iChannelNumber;
                entry.strPlot = program["et"].asString();

                Json::Value genres = program["g"];
                ostringstream generesStream;
                for (Json::Value::ArrayIndex genre = 0; genre < genres.size(); genre++) {
                    generesStream << genres[genre].asString() << " ";
                }
                entry.strGenreString = generesStream.str();

                if (channel)
                    channel->epg.insert(channel->epg.end(), entry);
            }
        }

        tempStart = tempEnd;
        tempEnd = tempStart + 3600*5; //Add 5 hours
    }
	
    return true;
}

