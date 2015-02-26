/**
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef __STOUT_NET_HPP__
#define __STOUT_NET_HPP__

#if defined(__linux__) || defined(__APPLE__)
#include <ifaddrs.h>
#endif
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <arpa/inet.h>

#ifdef __linux__
#include <linux/if.h>
#include <linux/if_packet.h>
#endif

#include <net/ethernet.h>

#ifdef __APPLE__
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#endif

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <curl/curl.h>

#include <iostream>
#include <set>
#include <string>

#include "abort.hpp"
#include "error.hpp"
#include "none.hpp"
#include "option.hpp"
#include "os.hpp"
#include "result.hpp"
#include "stringify.hpp"
#include "strings.hpp"
#include "try.hpp"


// Network utilities.
namespace net {

inline struct addrinfo createAddrInfo(int socktype, int family, int flags)
{
  struct addrinfo addr;
  memset(&addr, 0, sizeof(addr));
  addr.ai_socktype = socktype;
  addr.ai_family = family;
  addr.ai_flags |= flags;

  return addr;
}


// TODO(evelinad): Add createSockaddrIn6 when will support IPv6
inline struct sockaddr_in createSockaddrIn(uint32_t ip, int port)
{
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = ip;
  addr.sin_port = htons(port);

  return addr;
}


// Returns the HTTP response code resulting from attempting to
// download the specified HTTP or FTP URL into a file at the specified
// path.
inline Try<int> download(const std::string& url, const std::string& path)
{
  Try<int> fd = os::open(
      path,
      O_CREAT | O_WRONLY | O_CLOEXEC,
      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

  if (fd.isError()) {
    return Error(fd.error());
  }

  curl_global_init(CURL_GLOBAL_ALL);
  CURL* curl = curl_easy_init();

  if (curl == NULL) {
    curl_easy_cleanup(curl);
    os::close(fd.get());
    return Error("Failed to initialize libcurl");
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);

  FILE* file = fdopen(fd.get(), "w");
  if (file == NULL) {
    return ErrnoError("Failed to open file handle of '" + path + "'");
  }
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

  CURLcode curlErrorCode = curl_easy_perform(curl);
  if (curlErrorCode != 0) {
    curl_easy_cleanup(curl);
    fclose(file);
    return Error(curl_easy_strerror(curlErrorCode));
  }

  long code;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_cleanup(curl);

  if (fclose(file) != 0) {
    return ErrnoError("Failed to close file handle of '" + path + "'");
  }

  return Try<int>::some(code);
}


inline Try<std::string> hostname()
{
  char host[512];

  if (gethostname(host, sizeof(host)) < 0) {
    return ErrnoError();
  }

  // TODO(evelinad): Add AF_UNSPEC when we will support IPv6
  struct addrinfo hints = createAddrInfo(SOCK_STREAM, AF_INET, AI_CANONNAME);
  struct addrinfo *result;

  int error = getaddrinfo(host, NULL, &hints, &result);

  if (error != 0 || result == NULL) {
    if (result != NULL) {
      freeaddrinfo(result);
    }
    return Error(gai_strerror(error));
  }

  std::string hostname = result->ai_canonname;
  freeaddrinfo(result);

  return hostname;
}


// Returns a Try of the hostname for the provided IP. If the hostname
// cannot be resolved, then a string version of the IP address is
// returned.
inline Try<std::string> getHostname(uint32_t ip)
{
  sockaddr_in addr = createSockaddrIn(ip, 0);

  char hostname[MAXHOSTNAMELEN];
  int error = getnameinfo(
      (sockaddr*)&addr,
      sizeof(addr),
      hostname,
      MAXHOSTNAMELEN,
      NULL,
      0,
      0);
  if (error != 0) {
    return Error(std::string(gai_strerror(error)));
  }

  return std::string(hostname);
}


// Returns a Try of the IP for the provided hostname or an error if no
// IP is obtained.
inline Try<uint32_t> getIP(const std::string& hostname, sa_family_t family)
{
  struct addrinfo hints, *result;
  hints = createAddrInfo(SOCK_STREAM, family, 0);

  int error = getaddrinfo(hostname.c_str(), NULL, &hints, &result);
  if (error != 0 || result == NULL) {
    if (result != NULL ) {
      freeaddrinfo(result);
    }
    return Error(gai_strerror(error));
  }
  if (result->ai_addr == NULL) {
    freeaddrinfo(result);
    return Error("Got no addresses for '" + hostname + "'");
  }

  uint32_t ip = ((struct sockaddr_in*)(result->ai_addr))->sin_addr.s_addr;
  freeaddrinfo(result);

  return ip;
}


// Returns the names of all the link devices in the system.
inline Try<std::set<std::string> > links()
{
#if !defined(__linux__) && !defined(__APPLE__)
  return Error("Not implemented");
#else
  struct ifaddrs* ifaddr = NULL;
  if (getifaddrs(&ifaddr) == -1) {
    return ErrnoError();
  }

  std::set<std::string> names;
  for (struct ifaddrs* ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_name != NULL) {
      names.insert(ifa->ifa_name);
    }
  }

  freeifaddrs(ifaddr);
  return names;
#endif
}

} // namespace net {

#endif // __STOUT_NET_HPP__
