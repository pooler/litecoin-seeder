#include <algorithm>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <atomic>

#include "bitcoin.h"
#include "db.h"

using namespace std;

bool fTestNet = false;

class CDnsSeedOpts {
public:
  int nThreads;
  int nPort;
  int nP2Port;
  int nMinimumHeight;
  int nDnsThreads;
  int fUseTestNet;
  int fWipeBan;
  int fWipeIgnore;
  const char *mbox;
  const char *ns;
  const char *host;
  const char *tor;
  const char *ip_addr;
  const char *ipv4_proxy;
  const char *ipv6_proxy;
  const char *magic;
  std::vector<string> vSeeds;
  std::set<uint64_t> filter_whitelist;

  CDnsSeedOpts() : nThreads(96), nDnsThreads(4), ip_addr("::"), nPort(53), nP2Port(0), nMinimumHeight(0), mbox(NULL), ns(NULL), host(NULL), tor(NULL), fUseTestNet(false), fWipeBan(false), fWipeIgnore(false), ipv4_proxy(NULL), ipv6_proxy(NULL), magic(NULL) {}

  void ParseCommandLine(int argc, char **argv) {
    static const char *help = "Litecoin-seeder\n"
                              "Usage: %s -h <host> -n <ns> [-m <mbox>] [-t <threads>] [-p <port>]\n"
                              "\n"
                              "Options:\n"
                              "-s <seed>       Seed node to collect peers from (replaces default)\n"
                              "-h <host>       Hostname of the DNS seed\n"
                              "-n <ns>         Hostname of the nameserver\n"
                              "-m <mbox>       E-Mail address reported in SOA records\n"
                              "-t <threads>    Number of crawlers to run in parallel (default 96)\n"
                              "-d <threads>    Number of DNS server threads (default 4)\n"
                              "-a <address>    Address to listen on (default ::)\n"
                              "-p <port>       UDP port to listen on (default 53)\n"
                              "-o <ip:port>    Tor proxy IP/Port\n"
                              "-i <ip:port>    IPV4 SOCKS5 proxy IP/Port\n"
                              "-k <ip:port>    IPV6 SOCKS5 proxy IP/Port\n"
                              "-w f1,f2,...    Allow these flag combinations as filters\n"
                              "--p2port <port> P2P port to connect to\n"
                              "--magic <hex>   Magic string/network prefix\n"
                              "--minheight <n> Minimum height of block chain\n"
                              "--testnet       Use testnet\n"
                              "--wipeban       Wipe list of banned nodes\n"
                              "--wipeignore    Wipe list of ignored nodes\n"
                              "-?, --help      Show this text\n"
                              "\n";
    bool showHelp = false;

    while(1) {
      static struct option long_options[] = {
        {"seed", required_argument, 0, 's'},
        {"host", required_argument, 0, 'h'},
        {"ns",   required_argument, 0, 'n'},
        {"mbox", required_argument, 0, 'm'},
        {"threads", required_argument, 0, 't'},
        {"dnsthreads", required_argument, 0, 'd'},
        {"address", required_argument, 0, 'a'},
        {"port", required_argument, 0, 'p'},
        {"onion", required_argument, 0, 'o'},
        {"proxyipv4", required_argument, 0, 'i'},
        {"proxyipv6", required_argument, 0, 'k'},
        {"filter", required_argument, 0, 'w'},
        {"p2port", required_argument, 0, 'b'},
        {"magic", required_argument, 0, 'q'},
        {"minheight", required_argument, 0, 'x'},
        {"testnet", no_argument, &fUseTestNet, 1},
        {"wipeban", no_argument, &fWipeBan, 1},
        {"wipeignore", no_argument, &fWipeBan, 1},
        {"help", no_argument, 0, '?'},
        {0, 0, 0, 0}
      };
      int option_index = 0;
      int c = getopt_long(argc, argv, "s:h:n:m:t:a:p:d:o:i:k:w:b:q:x:", long_options, &option_index);
      if (c == -1) break;
      switch (c) {
        case 's': {
          vSeeds.emplace_back(optarg);
          break;
        }

        case 'h': {
          host = optarg;
          break;
        }

        case 'm': {
          mbox = optarg;
          break;
        }

        case 'n': {
          ns = optarg;
          break;
        }

        case 't': {
          int n = strtol(optarg, NULL, 10);
          if (n > 0 && n < 1000) nThreads = n;
          break;
        }

        case 'd': {
          int n = strtol(optarg, NULL, 10);
          if (n > 0 && n < 1000) nDnsThreads = n;
          break;
        }

        case 'a': {
          if (strchr(optarg, ':')==NULL) {
            char* ip4_addr = (char*) malloc(strlen(optarg)+8);
            strcpy(ip4_addr, "::FFFF:");
            strcat(ip4_addr, optarg);
            ip_addr = ip4_addr;
          } else {
            ip_addr = optarg;
          }
          break;
        }

        case 'p': {
          int p = strtol(optarg, NULL, 10);
          if (p > 0 && p < 65536) nPort = p;
          break;
        }

        case 'o': {
          tor = optarg;
          break;
        }

        case 'i': {
          ipv4_proxy = optarg;
          break;
        }

        case 'k': {
          ipv6_proxy = optarg;
          break;
        }

        case 'w': {
          char* ptr = optarg;
          while (*ptr != 0) {
            unsigned long l = strtoul(ptr, &ptr, 0);
            if (*ptr == ',') {
                ptr++;
            } else if (*ptr != 0) {
                break;
            }
            filter_whitelist.insert(l);
          }
          break;
        }

        case 'b': {
          int p = strtol(optarg, NULL, 10);
          if (p > 0 && p < 65536) nP2Port = p;
          break;
        }

        case 'q': {
          long int n;
          unsigned int c;
          if (strlen(optarg)!=8) {
            break; /* must be 4 hex-encoded bytes */
          }
          n = strtol(optarg, NULL, 16);
          if (n==0 && strcmp(optarg, "00000000")) {
            break; /* hex decode failed */
          }
          magic = optarg;
          break;
        }

        case 'x': {
          int n = strtol(optarg, NULL, 10);
          if (n > 0 && n <= 0x7fffffff) nMinimumHeight = n;
          break;
        }

        case '?': {
          showHelp = true;
          break;
        }
      }
    }
    if (filter_whitelist.empty()) {
        filter_whitelist.insert(NODE_NETWORK); // x1
        filter_whitelist.insert(NODE_NETWORK | NODE_BLOOM); // x5
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS); // x9
        filter_whitelist.insert(NODE_NETWORK | NODE_MWEB); // x1000000
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS | NODE_MWEB); // x1000009
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS | NODE_COMPACT_FILTERS); // x49
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS | NODE_COMPACT_FILTERS | NODE_MWEB); // x1000049
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS | NODE_COMPACT_FILTERS | NODE_MWEB | NODE_MWEB_LIGHT_CLIENT); //0x1800049
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS | NODE_P2P_V2); // x809
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS | NODE_P2P_V2 | NODE_COMPACT_FILTERS); //x849
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS | NODE_COMPACT_FILTERS | NODE_MWEB); // 0x1000049
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS | NODE_P2P_V2 | NODE_COMPACT_FILTERS | NODE_MWEB); //0x1000849
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS | NODE_COMPACT_FILTERS | NODE_MWEB | NODE_MWEB_LIGHT_CLIENT); //0x1800049
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS | NODE_P2P_V2 | NODE_COMPACT_FILTERS | NODE_MWEB | NODE_MWEB_LIGHT_CLIENT); //0x1800849
        filter_whitelist.insert(NODE_NETWORK | NODE_WITNESS | NODE_BLOOM); // xd
        filter_whitelist.insert(NODE_NETWORK_LIMITED); // x400
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_MWEB); // x1000400
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_BLOOM); // x404
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_WITNESS); // x408
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_WITNESS | NODE_MWEB); // x1000408
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_WITNESS | NODE_COMPACT_FILTERS); // x448
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_WITNESS | NODE_COMPACT_FILTERS | NODE_MWEB); // 0x1000448
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_WITNESS | NODE_COMPACT_FILTERS | NODE_MWEB | NODE_MWEB_LIGHT_CLIENT); // 0x1800448
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_WITNESS | NODE_P2P_V2); // xc08
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_WITNESS | NODE_P2P_V2 | NODE_COMPACT_FILTERS); // xc48
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_WITNESS | NODE_P2P_V2 | NODE_COMPACT_FILTERS | NODE_MWEB); // 0x1000c48
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_WITNESS | NODE_P2P_V2 | NODE_COMPACT_FILTERS | NODE_MWEB | NODE_MWEB_LIGHT_CLIENT); // 0x1800c48
        filter_whitelist.insert(NODE_NETWORK_LIMITED | NODE_WITNESS | NODE_BLOOM); // x40c
    }
    if (host != NULL && ns == NULL) showHelp = true;
    if (showHelp) {
        fprintf(stderr, help, argv[0]);
        exit(0);
    }
  }
};

#include "dns.h"

CAddrDb db;

extern "C" void* ThreadCrawler(void* data) {
  int *nThreads=(int*)data;
  do {
    std::vector<CServiceResult> ips;
    int wait = 5;
    db.GetMany(ips, 16, wait);
    int64 now = time(NULL);
    if (ips.empty()) {
      wait *= 1000;
      wait += rand() % (500 * *nThreads);
      Sleep(wait);
      continue;
    }
    vector<CAddress> addr;
    for (int i=0; i<ips.size(); i++) {
      CServiceResult &res = ips[i];
      res.nBanTime = 0;
      res.nClientV = 0;
      res.nHeight = 0;
      res.strClientV = "";
      res.services = 0;
      bool getaddr = res.ourLastSuccess + 86400 < now;
      res.fGood = TestNode(res.service,res.nBanTime,res.nClientV,res.strClientV,res.nHeight,getaddr ? &addr : NULL, res.services);
    }
    db.ResultMany(ips);
    db.Add(addr);
  } while(1);
  return nullptr;
}

extern "C" int GetIPList(void *thread, char *requestedHostname, addr_t *addr, int max, int ipv4, int ipv6);

class CDnsThread {
public:
  struct FlagSpecificData {
      int nIPv4, nIPv6;
      std::vector<addr_t> cache;
      time_t cacheTime;
      unsigned int cacheHits;
      FlagSpecificData() : nIPv4(0), nIPv6(0), cacheTime(0), cacheHits(0) {}
  };

  dns_opt_t dns_opt; // must be first
  const int id;
  std::map<uint64_t, FlagSpecificData> perflag;
  std::atomic<uint64_t> dbQueries;
  std::set<uint64_t> filterWhitelist;

  void cacheHit(uint64_t requestedFlags, bool force = false) {
    static bool nets[NET_MAX] = {};
    if (!nets[NET_IPV4]) {
        nets[NET_IPV4] = true;
        nets[NET_IPV6] = true;
    }
    time_t now = time(NULL);
    FlagSpecificData& thisflag = perflag[requestedFlags];
    thisflag.cacheHits++;
    if (force || thisflag.cacheHits * 400 > (thisflag.cache.size()*thisflag.cache.size()) || (thisflag.cacheHits*thisflag.cacheHits * 20 > thisflag.cache.size() && (now - thisflag.cacheTime > 5))) {
      set<CNetAddr> ips;
      db.GetIPs(ips, requestedFlags, 1000, nets);
      dbQueries++;
      thisflag.cache.clear();
      thisflag.nIPv4 = 0;
      thisflag.nIPv6 = 0;
      thisflag.cache.reserve(ips.size());
      for (set<CNetAddr>::iterator it = ips.begin(); it != ips.end(); it++) {
        struct in_addr addr;
        struct in6_addr addr6;
        if ((*it).GetInAddr(&addr)) {
          addr_t a;
          a.v = 4;
          memcpy(&a.data.v4, &addr, 4);
          thisflag.cache.push_back(a);
          thisflag.nIPv4++;
        } else if ((*it).GetIn6Addr(&addr6)) {
          addr_t a;
          a.v = 6;
          memcpy(&a.data.v6, &addr6, 16);
          thisflag.cache.push_back(a);
          thisflag.nIPv6++;
        }
      }
      thisflag.cacheHits = 0;
      thisflag.cacheTime = now;
    }
  }

  CDnsThread(CDnsSeedOpts* opts, int idIn) : id(idIn) {
    dns_opt.host = opts->host;
    dns_opt.ns = opts->ns;
    dns_opt.mbox = opts->mbox;
    dns_opt.datattl = 3600;
    dns_opt.nsttl = 40000;
    dns_opt.cb = GetIPList;
    dns_opt.addr = opts->ip_addr;
    dns_opt.port = opts->nPort;
    dns_opt.nRequests = 0;
    dbQueries = 0;
    perflag.clear();
    filterWhitelist = opts->filter_whitelist;
  }

  void run() {
    dnsserver(&dns_opt);
  }
};

extern "C" int GetIPList(void *data, char *requestedHostname, addr_t* addr, int max, int ipv4, int ipv6) {
  CDnsThread *thread = (CDnsThread*)data;

  uint64_t requestedFlags = 0;
  int hostlen = strlen(requestedHostname);
  if (hostlen > 1 && requestedHostname[0] == 'x' && requestedHostname[1] != '0') {
    char *pEnd;
    uint64_t flags = (uint64_t)strtoull(requestedHostname+1, &pEnd, 16);
    if (*pEnd == '.' && pEnd <= requestedHostname+17 && std::find(thread->filterWhitelist.begin(), thread->filterWhitelist.end(), flags) != thread->filterWhitelist.end())
      requestedFlags = flags;
    else
      return 0;
  }
  else if (strcasecmp(requestedHostname, thread->dns_opt.host))
    return 0;
  thread->cacheHit(requestedFlags);
  auto& thisflag = thread->perflag[requestedFlags];
  unsigned int size = thisflag.cache.size();
  unsigned int maxmax = (ipv4 ? thisflag.nIPv4 : 0) + (ipv6 ? thisflag.nIPv6 : 0);
  if (max > size)
    max = size;
  if (max > maxmax)
    max = maxmax;
  int i=0;
  while (i<max) {
    int j = i + (rand() % (size - i));
    do {
        bool ok = (ipv4 && thisflag.cache[j].v == 4) ||
                  (ipv6 && thisflag.cache[j].v == 6);
        if (ok) break;
        j++;
        if (j==size)
            j=i;
    } while(1);
    addr[i] = thisflag.cache[j];
    thisflag.cache[j] = thisflag.cache[i];
    thisflag.cache[i] = addr[i];
    i++;
  }
  return max;
}

vector<CDnsThread*> dnsThread;

extern "C" void* ThreadDNS(void* arg) {
  CDnsThread *thread = (CDnsThread*)arg;
  thread->run();
  return nullptr;
}

int StatCompare(const CAddrReport& a, const CAddrReport& b) {
  if (a.uptime[4] == b.uptime[4]) {
    if (a.uptime[3] == b.uptime[3]) {
      return a.clientVersion > b.clientVersion;
    } else {
      return a.uptime[3] > b.uptime[3];
    }
  } else {
    return a.uptime[4] > b.uptime[4];
  }
}

extern "C" void* ThreadDumper(void*) {
  int count = 0;
  do {
    Sleep(100000 << count); // First 100s, than 200s, 400s, 800s, 1600s, and then 3200s forever
    if (count < 5)
        count++;
    {
      vector<CAddrReport> v = db.GetAll();
      sort(v.begin(), v.end(), StatCompare);
      FILE *f = fopen("dnsseed.dat.new","w+");
      if (f) {
        {
          CAutoFile cf(f);
          cf << db;
        }
        rename("dnsseed.dat.new", "dnsseed.dat");
      }
      FILE *d = fopen("dnsseed.dump", "w");
      fprintf(d, "# address                                        good  lastSuccess    %%(2h)   %%(8h)   %%(1d)   %%(7d)  %%(30d)  blocks      svcs  version\n");
      double stat[5]={0,0,0,0,0};
      for (vector<CAddrReport>::const_iterator it = v.begin(); it < v.end(); it++) {
        CAddrReport rep = *it;
        fprintf(d, "%-47s  %4d  %11" PRId64 "  %6.2f%% %6.2f%% %6.2f%% %6.2f%% %6.2f%%  %6i  %08" PRIx64 "  %5i \"%s\"\n", rep.ip.ToString().c_str(), (int)rep.fGood, rep.lastSuccess, 100.0*rep.uptime[0], 100.0*rep.uptime[1], 100.0*rep.uptime[2], 100.0*rep.uptime[3], 100.0*rep.uptime[4], rep.blocks, rep.services, rep.clientVersion, rep.clientSubVersion.c_str());
        stat[0] += rep.uptime[0];
        stat[1] += rep.uptime[1];
        stat[2] += rep.uptime[2];
        stat[3] += rep.uptime[3];
        stat[4] += rep.uptime[4];
      }
      fclose(d);
      FILE *ff = fopen("dnsstats.log", "a");
      fprintf(ff, "%llu %g %g %g %g %g\n", (unsigned long long)(time(NULL)), stat[0], stat[1], stat[2], stat[3], stat[4]);
      fclose(ff);
    }
  } while(1);
  return nullptr;
}

extern "C" void* ThreadStats(void*) {
  bool first = true;
  do {
    char c[256];
    time_t tim = time(NULL);
    struct tm *tmp = localtime(&tim);
    strftime(c, 256, "[%y-%m-%d %H:%M:%S]", tmp);
    CAddrDbStats stats;
    db.GetStats(stats);
    if (first)
    {
      first = false;
      printf("\n\n\n\x1b[3A");
    }
    else
      printf("\x1b[2K\x1b[u");
    printf("\x1b[s");
    uint64_t requests = 0;
    uint64_t queries = 0;
    for (unsigned int i=0; i<dnsThread.size(); i++) {
      requests += dnsThread[i]->dns_opt.nRequests;
      queries += dnsThread[i]->dbQueries;
    }
    printf("%s %i/%i available (%i tried in %is, %i new, %i active), %i banned; %llu DNS requests, %llu db queries", c, stats.nGood, stats.nAvail, stats.nTracked, stats.nAge, stats.nNew, stats.nAvail - stats.nTracked - stats.nNew, stats.nBanned, (unsigned long long)requests, (unsigned long long)queries);
    Sleep(1000);
  } while(1);
  return nullptr;
}

static const string mainnet_seeds[] = {"dnsseed.litecoinpool.org", "seed-a.litecoin.loshan.co.uk", "dnsseed.thrasher.io", ""};
static const string testnet_seeds[] = {"seed-b.litecoin.loshan.co.uk", "dnsseed-testnet.thrasher.io", ""};
static const string *seeds = mainnet_seeds;
static vector<string> vSeeds;

extern "C" void* ThreadSeeder(void*) {
  vector<string> vDnsSeeds;
  for (const string& seed: vSeeds) {
    size_t len = seed.size();
    if (len > 6 && !seed.compare(len - 6, 6, ".onion")) {
      db.Add(CService(seed.c_str(), GetDefaultPort()), true);
    } else {
      vDnsSeeds.push_back(seed);
    }
  }
  do {
    for (const string& seed: vDnsSeeds) {
      vector<CNetAddr> ips;
      LookupHost(seed.c_str(), ips);
      for (vector<CNetAddr>::iterator it = ips.begin(); it != ips.end(); it++) {
        db.Add(CService(*it, GetDefaultPort()), true);
      }
    }
    Sleep(1800000);
  } while(1);
  return nullptr;
}

int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);
  setbuf(stdout, NULL);
  CDnsSeedOpts opts;
  opts.ParseCommandLine(argc, argv);
  printf("Supporting whitelisted filters: ");
  for (std::set<uint64_t>::const_iterator it = opts.filter_whitelist.begin(); it != opts.filter_whitelist.end(); it++) {
      if (it != opts.filter_whitelist.begin()) {
          printf(",");
      }
      printf("0x%lx", (unsigned long)*it);
  }
  printf("\n");
  if (opts.tor) {
    CService service(opts.tor, 9050);
    if (service.IsValid()) {
      printf("Using Tor proxy at %s\n", service.ToStringIPPort().c_str());
      SetProxy(NET_TOR, service);
    }
  }
  if (opts.ipv4_proxy) {
    CService service(opts.ipv4_proxy, 9050);
    if (service.IsValid()) {
      printf("Using IPv4 proxy at %s\n", service.ToStringIPPort().c_str());
      SetProxy(NET_IPV4, service);
    }
  }
  if (opts.ipv6_proxy) {
    CService service(opts.ipv6_proxy, 9050);
    if (service.IsValid()) {
      printf("Using IPv6 proxy at %s\n", service.ToStringIPPort().c_str());
      SetProxy(NET_IPV6, service);
    }
  }
  bool fDNS = true;
  if (opts.fUseTestNet) {
      printf("Using testnet.\n");
      pchMessageStart[0] = 0xfd;
      pchMessageStart[1] = 0xd2;
      pchMessageStart[2] = 0xc8;
      pchMessageStart[3] = 0xf1;
      seeds = testnet_seeds;
      fTestNet = true;
  }
  if (opts.nP2Port) {
    printf("Using P2P port %i\n", opts.nP2Port);
    nDefaultP2Port = opts.nP2Port;
  }
  if (opts.magic) {
    printf("Using magic %s\n", opts.magic);
    for (int n=0; n<4; ++n) {
      unsigned int c = 0;
      sscanf(&opts.magic[n*2], "%2x", &c);
      pchMessageStart[n] = (unsigned char) (c & 0xff);
    }
  }
  if (opts.nMinimumHeight) {
    printf("Using minimum height %i\n", opts.nMinimumHeight);
    nMinimumHeight = opts.nMinimumHeight;
  }
  if (!opts.vSeeds.empty()) {
    printf("Overriding DNS seeds\n");
    swap(opts.vSeeds, vSeeds);
  } else {
    for (int i=0; seeds[i][0]; i++) {
      vSeeds.emplace_back(seeds[i]);
    }
  }
  if (!opts.ns) {
    printf("No nameserver set. Not starting DNS server.\n");
    fDNS = false;
  }
  if (fDNS && !opts.host) {
    fprintf(stderr, "No hostname set. Please use -h.\n");
    exit(1);
  }
  if (fDNS && !opts.mbox) {
    fprintf(stderr, "No e-mail address set. Please use -m.\n");
    exit(1);
  }
  FILE *f = fopen("dnsseed.dat","r");
  if (f) {
    printf("Loading dnsseed.dat...");
    CAutoFile cf(f);
    cf >> db;
    if (opts.fWipeBan)
        db.banned.clear();
    if (opts.fWipeIgnore)
        db.ResetIgnores();
    printf("done\n");
  }
  pthread_t threadDns, threadSeed, threadDump, threadStats;
  if (fDNS) {
    printf("Starting %i DNS threads for %s on %s (port %i)...", opts.nDnsThreads, opts.host, opts.ns, opts.nPort);
    dnsThread.clear();
    for (int i=0; i<opts.nDnsThreads; i++) {
      dnsThread.push_back(new CDnsThread(&opts, i));
      pthread_create(&threadDns, NULL, ThreadDNS, dnsThread[i]);
      printf(".");
      Sleep(20);
    }
    printf("done\n");
  }
  printf("Starting seeder...");
  pthread_create(&threadSeed, NULL, ThreadSeeder, NULL);
  printf("done\n");
  printf("Starting %i crawler threads...", opts.nThreads);
  pthread_attr_t attr_crawler;
  pthread_attr_init(&attr_crawler);
  pthread_attr_setstacksize(&attr_crawler, 0x20000);
  for (int i=0; i<opts.nThreads; i++) {
    pthread_t thread;
    pthread_create(&thread, &attr_crawler, ThreadCrawler, &opts.nThreads);
  }
  pthread_attr_destroy(&attr_crawler);
  printf("done\n");
  pthread_create(&threadStats, NULL, ThreadStats, NULL);
  pthread_create(&threadDump, NULL, ThreadDumper, NULL);
  void* res;
  pthread_join(threadDump, &res);
  return 0;
}
