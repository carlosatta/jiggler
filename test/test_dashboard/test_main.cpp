#include <unity.h>

#include <set>
#include <string>

#include "api_router.h"
#include "controller.h"
#include "dashboard_html.h"
#include "fakes.h"

using namespace jig;

void setUp() {}
void tearDown() {}

namespace {

struct Route {
  const char* method;
  const char* path;
  const char* body;
};

// Every (method, path) the dashboard page calls.
const Route kPageRoutes[] = {
    {"GET", "/api/status", ""},   {"POST", "/api/start", ""}, {"POST", "/api/pause", ""},
    {"POST", "/api/stop", ""},    {"POST", "/api/move", ""},  {"PUT", "/api/config", "{}"},
};

// All "/api/<name>" occurrences in the page source.
std::set<std::string> apiPathsIn(const std::string& html) {
  std::set<std::string> out;
  size_t pos = 0;
  while ((pos = html.find("/api/", pos)) != std::string::npos) {
    size_t end = pos + 5;
    while (end < html.size() && ((html[end] >= 'a' && html[end] <= 'z') || html[end] == '_')) end++;
    out.insert(html.substr(pos, end - pos));
    pos = end;
  }
  return out;
}

}  // namespace

void test_dashboard_only_references_real_routes() {
  const std::string html = kDashboardHtml;
  const std::set<std::string> referenced = apiPathsIn(html);

  std::set<std::string> known;
  for (const Route& r : kPageRoutes) known.insert(r.path);
  for (const std::string& p : referenced) TEST_ASSERT_TRUE_MESSAGE(known.count(p) == 1, p.c_str());
  for (const std::string& p : known) TEST_ASSERT_TRUE_MESSAGE(referenced.count(p) == 1, p.c_str());

  FakeClock clock;
  XorShiftRng rng(1);
  MemStore store;
  Controller c(clock, rng, store);
  c.begin();
  for (const Route& r : kPageRoutes) {
    HttpResponse res = handleApi(c, r.method, r.path, r.body);
    TEST_ASSERT_TRUE_MESSAGE(res.status != 404 && res.status != 405, r.path);
  }
}

void test_dashboard_is_self_contained_html() {
  const std::string html = kDashboardHtml;
  TEST_ASSERT_TRUE(html.find("<!doctype html>") == 0);
  TEST_ASSERT_TRUE(html.find("http://") == std::string::npos);
  TEST_ASSERT_TRUE(html.find("https://") == std::string::npos);
  TEST_ASSERT_TRUE(html.find("Authorization") != std::string::npos);
  TEST_ASSERT_TRUE(html.find("localStorage") != std::string::npos);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_dashboard_only_references_real_routes);
  RUN_TEST(test_dashboard_is_self_contained_html);
  return UNITY_END();
}
