/*
 *      Copyright (C) 2016 Team MrMC
 *      https://github.com/MrMC
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with MrMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "PlexDirectory.h"
#include "FileItem.h"
#include "URL.h"
#include "filesystem/Directory.h"
#include "guilib/LocalizeStrings.h"
#include "services/plex/PlexUtils.h"
#include "services/plex/PlexServices.h"
#include "utils/Base64URL.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "utils/XBMCTinyXML.h"
#include "utils/log.h"
#include "video/VideoDatabase.h"
#include "music/MusicDatabase.h"

using namespace XFILE;

CPlexDirectory::CPlexDirectory()
{ }

CPlexDirectory::~CPlexDirectory()
{ }

bool CPlexDirectory::GetDirectory(const CURL& url, CFileItemList &items)
{
  CLog::Log(LOGDEBUG, "CPlexDirectory::GetDirectory");

  std::string strUrl = url.Get();
  std::string section = URIUtils::GetFileName(strUrl);
  items.SetPath(strUrl);
  std::string basePath = strUrl;
  URIUtils::RemoveSlashAtEnd(basePath);
  basePath = URIUtils::GetFileName(basePath);

  CLog::Log(LOGDEBUG, "CPlexDirectory::GetDirectory strURL = %s", strUrl.c_str());

  CVideoDatabase vdatabase;
  vdatabase.Open();
  bool hasMovies = vdatabase.HasContent(VIDEODB_CONTENT_MOVIES);
  bool hasShows = vdatabase.HasContent(VIDEODB_CONTENT_TVSHOWS);
  vdatabase.Close();
  CMusicDatabase mdatabase;
  mdatabase.Open();
  bool hasMusic = mdatabase.HasContent();
  mdatabase.Close();

  
  if (StringUtils::StartsWithNoCase(strUrl, "plex://movies/"))
  {
    if (section.empty())
    {
      //look through all plex clients and pull content data for "movie" type
      std::vector<CPlexClientPtr> clients;
      CPlexServices::GetInstance().GetClients(clients);
      for (const auto &client : clients)
      {
        client->ClearSectionItems();
        std::vector<PlexSectionsContent> contents = client->GetMovieContent();
        if (contents.size() > 1 ||
            ((items.Size() > 0 || hasMovies || CServicesManager::GetInstance().HasEmbyServices() ||
              clients.size() > 1) && contents.size() == 1))
        {
          for (const auto &content : contents)
          {
            std::string title = client->FormatContentTitle(content.title);
            CFileItemPtr pItem(new CFileItem(title));
            pItem->m_bIsFolder = true;
            pItem->m_bIsShareOrDrive = true;
            CPlexUtils::SetPlexItemProperties(*pItem, client);
            // have to do it this way because raw url has authToken as protocol option
            CURL curl(client->GetUrl());
            curl.SetProtocol(client->GetProtocol());
            std::string filename = StringUtils::Format("%s/%s", content.section.c_str(), (basePath == "titles"? "all":""));
            curl.SetFileName(filename);
            pItem->SetPath("plex://movies/" + basePath + "/" + Base64URL::Encode(curl.Get()));
            pItem->SetLabel(title);
            std::string value = content.thumb;
            if (!value.empty() && (value[0] == '/'))
              StringUtils::TrimLeft(value, "/");
            curl.SetFileName(value);
            pItem->SetIconImage(curl.Get());
            items.Add(pItem);
            client->AddSectionItem(pItem);
            CLog::Log(LOGDEBUG, "CPlexDirectory::GetDirectory client(%s), title(%s)", client->GetServerName().c_str(), title.c_str());
          }
        }
        else if (contents.size() == 1)
        {
          CURL curl(client->GetUrl());
          curl.SetProtocol(client->GetProtocol());
          std::string filename = StringUtils::Format("%s/%s", contents[0].section.c_str(), (basePath == "titles"? "all":""));
          curl.SetFileName(filename);
          CDirectory::GetDirectory("plex://movies/" + basePath + "/" + Base64URL::Encode(curl.Get()), items);
          items.SetContent("movies");
          CPlexUtils::SetPlexItemProperties(items, client);
          for (int item = 0; item < items.Size(); ++item)
            CPlexUtils::SetPlexItemProperties(*items[item], client);
        }
        std::string label = basePath;
        if (URIUtils::GetFileName(basePath) == "recentlyaddedmovies")
          label = g_localizeStrings.Get(20386);
        else if (URIUtils::GetFileName(basePath) == "inprogressmovies")
          label = g_localizeStrings.Get(627);
        else
          StringUtils::ToCapitalize(label);
        items.SetLabel(label);
      }
    }
    else
    {
      CPlexClientPtr client = CPlexServices::GetInstance().FindClient(strUrl);
      if (!client || !client->GetPresence())
      {
        CLog::Log(LOGDEBUG, "CPlexDirectory::GetDirectory no client or client not present %s", CURL::GetRedacted(strUrl).c_str());
        return false;
      }

      items.ClearItems();
      std::string path = URIUtils::GetParentPath(strUrl);
      URIUtils::RemoveSlashAtEnd(path);
      path = URIUtils::GetFileName(path);

      std::string filter = "year";
      if (path == "genres")
        filter = "genre";
      else if (path == "actors")
        filter = "actor";
      else if (path == "directors")
        filter = "director";
      else if (path == "sets" || path == "collection")
        filter = "collection";
      else if (path == "countries" || path == "country")
        filter = "country";
      else if (path == "studios")
        filter = "studio";
      else if (path == "clear" || path.empty())
        filter = "";
      

      if (path == "titles" || path == "filter")
      {
        CPlexUtils::GetPlexMovies(items, Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(369));
        items.SetContent("movies");
      }
      else if (path == "recentlyaddedmovies")
      {
        CPlexUtils::GetPlexRecentlyAddedMovies(items, Base64URL::Decode(section), 25, true);
        items.SetLabel(g_localizeStrings.Get(20386));
        items.SetContent("movies");
      }
      else if (path == "inprogressmovies")
      {
        CPlexUtils::GetPlexInProgressMovies(items, Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(627));
        items.SetContent("movies");
      }
      else if (path == "videoplaylists")
      {
        CPlexUtils::GetPlexVideoPlaylistItems(items, Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(3));
        items.SetContent("playlists");
      }
      else if (path == "filters")
      {
        CPlexUtils::GetPlexFilters(items, Base64URL::Decode(section), "plex://movies/");
        items.SetLabel(g_localizeStrings.Get(369));
        items.SetContent("filters");
        items.AddSortMethod(SortByNone, 551, LABEL_MASKS("%F", "", "%L", ""));
        items.ClearSortState();
      }
      else
      {
        CPlexUtils::GetPlexFilter(items, Base64URL::Decode(section), "plex://movies/filter/", filter);
        items.SetContent("filters");
        items.AddSortMethod(SortByNone, 551, LABEL_MASKS("%F", "", "%L", ""));
      }
      CLog::Log(LOGDEBUG, "CPlexDirectory::GetDirectory' client(%s), found %d movies", client->GetServerName().c_str(), items.Size());
    }
    return true;
  }
  else if (StringUtils::StartsWithNoCase(strUrl, "plex://tvshows/"))
  {
    if (section.empty())
    {
      //look through all plex servers and pull content data for "show" type
      std::vector<CPlexClientPtr> clients;
      CPlexServices::GetInstance().GetClients(clients);
      for (const auto &client : clients)
      {
        client->ClearSectionItems();
        std::vector<PlexSectionsContent> contents = client->GetTvContent();
        if (contents.size() > 1 ||
            ((items.Size() > 0 || hasShows || CServicesManager::GetInstance().HasEmbyServices() ||
              clients.size() > 1) && contents.size() == 1))
        {
          for (const auto &content : contents)
          {
            std::string title = client->FormatContentTitle(content.title);
            CFileItemPtr pItem(new CFileItem(title));
            pItem->m_bIsFolder = true;
            pItem->m_bIsShareOrDrive = true;
            CPlexUtils::SetPlexItemProperties(*pItem, client);
            // have to do it this way because raw url has authToken as protocol option
            CURL curl(client->GetUrl());
            curl.SetProtocol(client->GetProtocol());
            std::string filename = StringUtils::Format("%s/%s", content.section.c_str(), (basePath == "titles"? "all":""));
            curl.SetFileName(filename);
            pItem->SetPath("plex://tvshows/" + basePath + "/" + Base64URL::Encode(curl.Get()));
            pItem->SetLabel(title);
            std::string value = content.thumb;
            if (!value.empty() && (value[0] == '/'))
              StringUtils::TrimLeft(value, "/");
            curl.SetFileName(value);
            pItem->SetIconImage(curl.Get());
            items.Add(pItem);
            client->AddSectionItem(pItem);
            CLog::Log(LOGDEBUG, "CPlexDirectory::GetDirectory client(%s), title(%s)", client->GetServerName().c_str(), title.c_str());
          }
        }
        else if (contents.size() == 1)
        {
          CURL curl(client->GetUrl());
          curl.SetProtocol(client->GetProtocol());
          std::string filename = StringUtils::Format("%s/%s", contents[0].section.c_str(), (basePath == "titles"? "all":""));
          curl.SetFileName(filename);
          CDirectory::GetDirectory("plex://tvshows/" + basePath + "/" + Base64URL::Encode(curl.Get()), items);
          CPlexUtils::SetPlexItemProperties(items, client);
          for (int item = 0; item < items.Size(); ++item)
            CPlexUtils::SetPlexItemProperties(*items[item], client);
        }
        std::string label = basePath;
        if (URIUtils::GetFileName(basePath) == "recentlyaddedepisodes")
          label = g_localizeStrings.Get(20387);
        else if (URIUtils::GetFileName(basePath) == "inprogressshows")
          label = g_localizeStrings.Get(626);
        else
          StringUtils::ToCapitalize(label);
        items.SetLabel(label);
      }
    }
    else
    {
      CPlexClientPtr client = CPlexServices::GetInstance().FindClient(strUrl);
      if (!client || !client->GetPresence())
      {
        CLog::Log(LOGDEBUG, "CPlexDirectory::GetDirectory no client or client not present %s", CURL::GetRedacted(strUrl).c_str());
        return false;
      }

      items.ClearItems();
      std::string path = URIUtils::GetParentPath(strUrl);
      URIUtils::RemoveSlashAtEnd(path);
      path = URIUtils::GetFileName(path);

      std::string filter = "year";
      if (path == "genres")
        filter = "genre";
      else if (path == "actors")
        filter = "actor";
      else if (path == "directors")
        filter = "director";
      else if (path == "sets" || path == "collection")
        filter = "collection";
      else if (path == "countries" || path == "country")
        filter = "country";
      else if (path == "studios")
        filter = "studio";
      else if (path == "clear" || path.empty())
        filter = "";

      if (path == "titles" || path == "filter")
      {
        CPlexUtils::GetPlexTvshows(items,Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(369));
        items.SetContent("tvshows");
      }
      else if (path == "shows")
      {
        CPlexUtils::GetPlexSeasons(items,Base64URL::Decode(section));
        if(items.Size() > 1 && items[1]->GetVideoInfoTag()->m_type == MediaTypeSeason)
          items.SetContent("tvshows");
        else
          items.SetContent("episodes");
      }
      else if (path == "seasons")
      {
        CPlexUtils::GetPlexEpisodes(items,Base64URL::Decode(section));
        items.SetContent("episodes");
      }
      else if (path == "recentlyaddedepisodes")
      {
        CPlexUtils::GetPlexRecentlyAddedEpisodes(items, Base64URL::Decode(section), 25, true);
        items.SetLabel(g_localizeStrings.Get(20387));
        items.SetContent("episodes");
      }
      else if (path == "inprogressshows")
      {
        CPlexUtils::GetPlexInProgressShows(items, Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(626));
        items.SetContent("episodes");
      }
      else if (path == "filters")
      {
        CPlexUtils::GetPlexFilters(items, Base64URL::Decode(section), "plex://tvshows/");
        items.SetLabel(g_localizeStrings.Get(369));
        items.AddSortMethod(SortByNone, 551, LABEL_MASKS("%F", "", "%L", ""));
        items.SetContent("filters");
      }
      else
      {
        CPlexUtils::GetPlexFilter(items, Base64URL::Decode(section), "plex://tvshows/filter/", filter);
        items.AddSortMethod(SortByNone, 551, LABEL_MASKS("%F", "", "%L", ""));
        items.SetContent("filters");
      }
      CLog::Log(LOGDEBUG, "CPlexDirectory::GetDirectory' client(%s), found %d shows", client->GetServerName().c_str(), items.Size());
    }
    return true;
  }
  else if (StringUtils::StartsWithNoCase(strUrl, "plex://music/"))
  {
    if (section.empty())
    {
      std::string pathsection = "";
      if (basePath == "artists")
        pathsection = "all";
      else if (basePath == "albums")
        pathsection = "all?type=9";
      else if (basePath == "songs")
        pathsection = "all?type=10";
      else if (basePath == "recentlyaddedalbums")
        pathsection = "recentlyAdded";
      //look through all plex servers and pull content data for "music" type
      std::vector<CPlexClientPtr> clients;
      CPlexServices::GetInstance().GetClients(clients);
      for (const auto &client : clients)
      {
        client->ClearSectionItems();
        std::vector<PlexSectionsContent> contents = client->GetArtistContent();
        if (contents.size() > 1 ||
            ((items.Size() > 0 || hasMusic || CServicesManager::GetInstance().HasEmbyServices() ||
              clients.size() > 1) && contents.size() == 1))
        {
          // multiple folders or providers found, add root folder for each node
          for (const auto &content : contents)
          {
            std::string title = client->FormatContentTitle(content.title);
            CFileItemPtr pItem(new CFileItem(title));
            pItem->m_bIsFolder = true;
            pItem->m_bIsShareOrDrive = true;
            CPlexUtils::SetPlexItemProperties(*pItem, client);
            // have to do it this way because raw url has authToken as protocol option
            CURL curl(client->GetUrl());
            curl.SetProtocol(client->GetProtocol());
            std::string filename = StringUtils::Format("%s/%s", content.section.c_str(), pathsection.c_str());
            curl.SetFileName(filename);
            pItem->SetPath("plex://music/" + basePath + "/" + Base64URL::Encode(curl.Get()));
            pItem->SetLabel(title);
            std::string value = content.thumb;
            if (!value.empty() && (value[0] == '/'))
              StringUtils::TrimLeft(value, "/");
            curl.SetFileName(value);
            pItem->SetIconImage(curl.Get());
            items.Add(pItem);
            client->AddSectionItem(pItem);
            CLog::Log(LOGDEBUG, "CPlexDirectory::GetDirectory client(%s), title(%s)", client->GetServerName().c_str(), title.c_str());
          }
        }
        else if (contents.size() == 1)
        {
          // only one folder/provider found, pull content directly
          CURL curl(client->GetUrl());
          curl.SetProtocol(client->GetProtocol());
          std::string filename = StringUtils::Format("%s/%s", contents[0].section.c_str(), pathsection.c_str());
          curl.SetFileName(filename);
          CDirectory::GetDirectory("plex://music/" + basePath + "/" + Base64URL::Encode(curl.Get()), items);
          items.SetContent("artists");
          CPlexUtils::SetPlexItemProperties(items, client);
          for (int item = 0; item < items.Size(); ++item)
            CPlexUtils::SetPlexItemProperties(*items[item], client);
        }
        std::string label = basePath;
        if (URIUtils::GetFileName(basePath) == "recentlyaddedalbums")
          label = g_localizeStrings.Get(359);
        items.SetLabel(label);
      }
    }
    else
    {
      CPlexClientPtr client = CPlexServices::GetInstance().FindClient(strUrl);
      if (!client || !client->GetPresence())
      {
        CLog::Log(LOGDEBUG, "CPlexDirectory::GetDirectory no client or client not present %s", CURL::GetRedacted(strUrl).c_str());
        return false;
      }

      items.ClearItems();
      std::string path = URIUtils::GetParentPath(strUrl);
      URIUtils::RemoveSlashAtEnd(path);
      path = URIUtils::GetFileName(path);
      
      std::string filter = "all";
      if (path == "albums")
        filter = "albums";
      
      if (path == "root" || path == "artists")
      {
        CPlexUtils::GetPlexArtistsOrAlbum(items,Base64URL::Decode(section), false);
        items.SetLabel(g_localizeStrings.Get(133));
        items.SetContent("artists");
      }
      else if (path == "albums")
      {
        CPlexUtils::GetPlexArtistsOrAlbum(items,Base64URL::Decode(section), true);
        items.SetLabel(g_localizeStrings.Get(132));
        items.SetContent("albums");
      }
      else if (path == "songs")
      {
        CPlexUtils::GetPlexSongs(items,Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(134));
        items.SetContent("songs");
      }
      else if (path == "recentlyaddedalbums")
      {
        CPlexUtils::GetPlexRecentlyAddedAlbums(items, Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(359));
        items.SetContent("albums");
      }
      else if (path == "musicplaylists")
      {
        CPlexUtils::GetPlexMusicPlaylistItems(items, Base64URL::Decode(section));
        items.SetLabel(g_localizeStrings.Get(134));
        items.SetContent("songs");
      }
      else if (path == "filters")
      {
        CPlexUtils::GetPlexMusicFilters(items, Base64URL::Decode(section), "plex://music/");
        items.AddSortMethod(SortByNone, 551, LABEL_MASKS("%F", "", "%L", ""));
        items.SetContent("filters");
      }
      else
      {
        std::string parentPath = "plex://music/filter/";
        if (path == "folder")
        {
          filter = "";
          parentPath = "plex://music/artists/";
        }
        CPlexUtils::GetPlexMusicFilter(items, Base64URL::Decode(section), parentPath, filter);
        items.AddSortMethod(SortByNone, 551, LABEL_MASKS("%F", "", "%L", ""));
        items.SetContent("filters");
      }
    }
    return true;
  }
  else
  {
    CLog::Log(LOGDEBUG, "CPlexDirectory::GetDirectory got nothing from %s", CURL::GetRedacted(strUrl).c_str());
  }

  return false;
}

DIR_CACHE_TYPE CPlexDirectory::GetCacheType(const CURL& url) const
{
  return DIR_CACHE_NEVER;
}
