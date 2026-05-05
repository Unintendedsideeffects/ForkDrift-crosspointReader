#if __has_include(<NetworkUdp.h>)

#include "WebDAVHandler.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_task_wdt.h>

#include "SpiBusMutex.h"

namespace {
const char* HIDDEN_ITEMS[] = {"System Volume Information", "XTCache"};
constexpr size_t HIDDEN_ITEMS_COUNT = sizeof(HIDDEN_ITEMS) / sizeof(HIDDEN_ITEMS[0]);

// RFC 1123 date format helper: "Sun, 06 Nov 1994 08:49:37 GMT"
// ESP32 doesn't have real-time clock set by default, so we use a fixed epoch date
// as a fallback. The date is not critical for WebDAV Class 1 operations.
const char* FIXED_DATE = "Thu, 01 Jan 2024 00:00:00 GMT";
}  // namespace

// ── RequestHandler interface ─────────────────────────────────────────────────

bool WebDAVHandler::canHandle(WebServer& server, HTTPMethod method, const String& uri) {
  (void)server;
  (void)uri;
  switch (method) {
    case HTTP_OPTIONS:
    case HTTP_PROPFIND:
    case HTTP_GET:
    case HTTP_HEAD:
    case HTTP_PUT:
    case HTTP_DELETE:
    case HTTP_MKCOL:
    case HTTP_MOVE:
    case HTTP_COPY:
    case HTTP_LOCK:
    case HTTP_UNLOCK:
      return true;
    default:
      return false;
  }
}

bool WebDAVHandler::canRaw(WebServer& server, const String& uri) {
  (void)uri;
  return server.method() == HTTP_PUT;
}

void WebDAVHandler::raw(WebServer& server, const String& uri, HTTPRaw& raw) {
  (void)uri;
  if (raw.status == RAW_START) {
    _putPath = getRequestPath(server);
    _putTempPath = _putPath + ".davtmp";
    _putBackupPath = _putPath + ".davbak";
    if (isProtectedPath(_putPath)) {
      _putOk = false;
      return;
    }

    // Ensure parent directory exists
    int lastSlash = _putPath.lastIndexOf('/');
    if (lastSlash > 0) {
      String parentPath = _putPath.substring(0, lastSlash);
      if (!existsLocked(parentPath)) {
        _putOk = false;
        return;
      }
    }

    if (_putFile) closeLocked(_putFile);
    _putExisted = existsLocked(_putPath);

    if (_putExisted) {
      FsFile existing = openLocked(_putPath);
      bool existingIsDirectory = false;
      if (existing) {
        SpiBusMutex::Guard guard;
        existingIsDirectory = existing.isDirectory();
      }
      if (existing) closeLocked(existing);
      if (existingIsDirectory) {
        _putOk = false;
        return;
      }
    }

    // Write to a temp file to avoid destroying the original on failed upload
    removeLocked(_putTempPath);
    removeLocked(_putBackupPath);
    {
      SpiBusMutex::Guard guard;
      _putOk = Storage.openFileForWrite("DAV", _putTempPath, _putFile);
    }
    LOG_DBG("DAV", "PUT START: %s", _putPath.c_str());

  } else if (raw.status == RAW_WRITE) {
    if (_putFile && _putOk) {
      esp_task_wdt_reset();
      size_t written = 0;
      {
        SpiBusMutex::Guard guard;
        written = _putFile.write(raw.buf, raw.currentSize);
      }
      if (written != raw.currentSize) {
        _putOk = false;
      }
    }

  } else if (raw.status == RAW_END) {
    if (_putFile) closeLocked(_putFile);
    if (_putOk) {
      _putOk = finalizePutTarget();
    }
    LOG_DBG("DAV", "PUT END: %u bytes, ok=%d", raw.totalSize, _putOk);

  } else if (raw.status == RAW_ABORTED) {
    if (_putFile) closeLocked(_putFile);
    removeLocked(_putTempPath);
    removeLocked(_putBackupPath);
    _putOk = false;
  }
}

bool WebDAVHandler::handle(WebServer& server, HTTPMethod method, const String& uri) {
  (void)uri;
  switch (method) {
    case HTTP_OPTIONS:
      handleOptions(server);
      return true;
    case HTTP_PROPFIND:
      handlePropfind(server);
      return true;
    case HTTP_GET:
      handleGet(server);
      return true;
    case HTTP_HEAD:
      handleHead(server);
      return true;
    case HTTP_PUT:
      handlePut(server);
      return true;
    case HTTP_DELETE:
      handleDelete(server);
      return true;
    case HTTP_MKCOL:
      handleMkcol(server);
      return true;
    case HTTP_MOVE:
      handleMove(server);
      return true;
    case HTTP_COPY:
      handleCopy(server);
      return true;
    case HTTP_LOCK:
      handleLock(server);
      return true;
    case HTTP_UNLOCK:
      handleUnlock(server);
      return true;
    default:
      return false;
  }
}

// ── OPTIONS ──────────────────────────────────────────────────────────────────

void WebDAVHandler::handleOptions(WebServer& s) {
  s.sendHeader("DAV", "1");
  s.sendHeader("Allow",
               "OPTIONS, GET, HEAD, PUT, DELETE, "
               "PROPFIND, MKCOL, MOVE, COPY, LOCK, UNLOCK");
  s.sendHeader("MS-Author-Via", "DAV");
  s.send(200);
  LOG_DBG("DAV", "OPTIONS %s", s.uri().c_str());
}

// ── PROPFIND ─────────────────────────────────────────────────────────────────

void WebDAVHandler::handlePropfind(WebServer& s) {
  String path = getRequestPath(s);
  int depth = getDepth(s);

  LOG_DBG("DAV", "PROPFIND %s depth=%d", path.c_str(), depth);

  // Check if path exists
  if (!existsLocked(path) && path != "/") {
    s.send(404, "text/plain", "Not Found");
    return;
  }

  FsFile root = openLocked(path);
  if (!root) {
    if (path == "/") {
      // Root should always work — send minimal response
      s.setContentLength(CONTENT_LENGTH_UNKNOWN);
      s.send(207, "application/xml; charset=\"utf-8\"", "");
      s.sendContent(
          "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
          "<D:multistatus xmlns:D=\"DAV:\">\n");
      sendPropEntry(s, "/", true, 0, FIXED_DATE);
      s.sendContent("</D:multistatus>\n");
      s.sendContent("");
      return;
    }
    s.send(500, "text/plain", "Failed to open");
    return;
  }

  bool isDir = false;
  size_t fileSize = 0;
  {
    SpiBusMutex::Guard guard;
    isDir = root.isDirectory();
    if (!isDir) {
      fileSize = root.size();
    }
  }

  s.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s.send(207, "application/xml; charset=\"utf-8\"", "");
  s.sendContent(
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<D:multistatus xmlns:D=\"DAV:\">\n");

  // Entry for the resource itself
  if (isDir) {
    sendPropEntry(s, path, true, 0, FIXED_DATE);
  } else {
    sendPropEntry(s, path, false, fileSize, FIXED_DATE);
    closeLocked(root);
    s.sendContent("</D:multistatus>\n");
    s.sendContent("");
    return;
  }

  // If depth > 0 and it's a directory, list children
  if (depth > 0) {
    while (true) {
      String fileName;
      bool childIsDirectory = false;
      size_t childSize = 0;
      bool shouldHide = false;

      {
        SpiBusMutex::Guard guard;
        FsFile file = root.openNextFile();
        if (!file) {
          break;
        }

        char name[500];
        if (!file.getName(name, sizeof(name))) {
          file.close();
          continue;
        }
        fileName = String(name);

        shouldHide = fileName.startsWith(".");
        if (!shouldHide) {
          for (size_t i = 0; i < HIDDEN_ITEMS_COUNT; i++) {
            if (fileName.equals(HIDDEN_ITEMS[i])) {
              shouldHide = true;
              break;
            }
          }
        }

        if (!shouldHide) {
          childIsDirectory = file.isDirectory();
          if (!childIsDirectory) {
            childSize = file.size();
          }
        }

        file.close();
      }

      if (!shouldHide) {
        String childPath = path;
        if (!childPath.endsWith("/")) childPath += "/";
        childPath += fileName;

        if (childIsDirectory) {
          sendPropEntry(s, childPath, true, 0, FIXED_DATE);
        } else {
          sendPropEntry(s, childPath, false, childSize, FIXED_DATE);
        }
      }

      yield();
      esp_task_wdt_reset();
    }
  }

  closeLocked(root);
  s.sendContent("</D:multistatus>\n");
  s.sendContent("");
}

void WebDAVHandler::sendPropEntry(WebServer& s, const String& path, bool isDir, size_t size,
                                  const String& lastModified) const {
  String href;
  urlEncodePath(path, href);
  // Ensure directory hrefs end with /
  if (isDir && !href.endsWith("/")) href += "/";

  String xml = "<D:response><D:href>";
  xml += href;
  xml += "</D:href><D:propstat><D:prop>";

  if (isDir) {
    xml += "<D:resourcetype><D:collection/></D:resourcetype>";
  } else {
    xml += "<D:resourcetype/>";
    xml += "<D:getcontentlength>";
    xml += String(size);
    xml += "</D:getcontentlength>";
    String mime = getMimeType(path);
    xml += "<D:getcontenttype>";
    xml += mime;
    xml += "</D:getcontenttype>";
  }

  xml += "<D:getlastmodified>";
  xml += lastModified;
  xml += "</D:getlastmodified>";

  xml += "</D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response>\n";

  s.sendContent(xml);
}

// ── GET ──────────────────────────────────────────────────────────────────────

void WebDAVHandler::handleGet(WebServer& s) {
  String path = getRequestPath(s);
  LOG_DBG("DAV", "GET %s", path.c_str());

  if (isProtectedPath(path)) {
    s.send(403, "text/plain", "Forbidden");
    return;
  }

  if (!existsLocked(path)) {
    s.send(404, "text/plain", "Not Found");
    return;
  }

  FsFile file = openLocked(path);
  if (!file) {
    s.send(500, "text/plain", "Failed to open file");
    return;
  }

  bool isDirectory = false;
  size_t fileSize = 0;
  {
    SpiBusMutex::Guard guard;
    isDirectory = file.isDirectory();
    if (!isDirectory) {
      fileSize = file.size();
    }
  }
  if (isDirectory) {
    closeLocked(file);
    // For directories, return a PROPFIND-like response or redirect
    s.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  String contentType = getMimeType(path);
  s.setContentLength(fileSize);
  s.send(200, contentType.c_str(), "");

  uint8_t buffer[4096];
  WiFiClient client = s.client();
  while (true) {
    esp_task_wdt_reset();
    size_t bytesRead = 0;
    {
      SpiBusMutex::Guard guard;
      bytesRead = file.read(buffer, sizeof(buffer));
    }
    if (bytesRead == 0) {
      break;
    }

    size_t totalWritten = 0;
    while (totalWritten < bytesRead) {
      esp_task_wdt_reset();
      const size_t written = client.write(buffer + totalWritten, bytesRead - totalWritten);
      if (written == 0) {
        closeLocked(file);
        return;
      }
      totalWritten += written;
    }
  }
  closeLocked(file);
}

// ── HEAD ─────────────────────────────────────────────────────────────────────

void WebDAVHandler::handleHead(WebServer& s) {
  String path = getRequestPath(s);
  LOG_DBG("DAV", "HEAD %s", path.c_str());

  if (isProtectedPath(path)) {
    s.send(403, "text/plain", "");
    return;
  }

  if (!existsLocked(path)) {
    s.send(404, "text/plain", "");
    return;
  }

  FsFile file = openLocked(path);
  if (!file) {
    s.send(500, "text/plain", "");
    return;
  }

  bool isDirectory = false;
  size_t fileSize = 0;
  {
    SpiBusMutex::Guard guard;
    isDirectory = file.isDirectory();
    if (!isDirectory) {
      fileSize = file.size();
    }
  }

  if (isDirectory) {
    closeLocked(file);
    s.send(200, "text/html", "");
    return;
  }

  String contentType = getMimeType(path);
  s.setContentLength(fileSize);
  s.send(200, contentType.c_str(), "");
  closeLocked(file);
}

// ── PUT ──────────────────────────────────────────────────────────────────────

void WebDAVHandler::handlePut(WebServer& s) {
  // Body was already received via canRaw/raw callbacks
  String path = getRequestPath(s);
  LOG_DBG("DAV", "PUT %s", path.c_str());

  if (isProtectedPath(path)) {
    s.send(403, "text/plain", "Forbidden");
    return;
  }

  if (!_putOk) {
    removeLocked(_putTempPath);
    removeLocked(_putBackupPath);
    s.send(500, "text/plain", "Write failed - incomplete upload or disk full");
    return;
  }

  clearEpubCacheIfNeeded(path);
  s.send(_putExisted ? 204 : 201);
  LOG_DBG("DAV", "PUT complete: %s", path.c_str());
}

// ── DELETE ───────────────────────────────────────────────────────────────────

void WebDAVHandler::handleDelete(WebServer& s) {
  String path = getRequestPath(s);
  LOG_DBG("DAV", "DELETE %s", path.c_str());

  if (path == "/" || path.isEmpty()) {
    s.send(403, "text/plain", "Cannot delete root");
    return;
  }

  if (isProtectedPath(path)) {
    s.send(403, "text/plain", "Forbidden");
    return;
  }

  if (!existsLocked(path)) {
    s.send(404, "text/plain", "Not Found");
    return;
  }

  FsFile file = openLocked(path);
  if (!file) {
    s.send(500, "text/plain", "Failed to open");
    return;
  }

  bool isDirectory = false;
  {
    SpiBusMutex::Guard guard;
    isDirectory = file.isDirectory();
  }

  if (isDirectory) {
    // Check if directory is empty
    FsFile entry;
    {
      SpiBusMutex::Guard guard;
      entry = file.openNextFile();
    }
    if (entry) {
      closeLocked(entry);
      closeLocked(file);
      s.send(409, "text/plain", "Directory not empty");
      return;
    }
    closeLocked(file);
    if (rmdirLocked(path)) {
      s.send(204);
    } else {
      s.send(500, "text/plain", "Failed to remove directory");
    }
  } else {
    closeLocked(file);
    clearEpubCacheIfNeeded(path);
    if (removeLocked(path)) {
      s.send(204);
    } else {
      s.send(500, "text/plain", "Failed to delete file");
    }
  }
}

// ── MKCOL ────────────────────────────────────────────────────────────────────

void WebDAVHandler::handleMkcol(WebServer& s) {
  String path = getRequestPath(s);
  LOG_DBG("DAV", "MKCOL %s", path.c_str());

  if (isProtectedPath(path)) {
    s.send(403, "text/plain", "Forbidden");
    return;
  }

  // MKCOL must not have a body (RFC 4918)
  if (s.clientContentLength() > 0) {
    s.send(415, "text/plain", "Unsupported Media Type");
    return;
  }

  if (existsLocked(path)) {
    s.send(405, "text/plain", "Already exists");
    return;
  }

  // Check parent exists
  int lastSlash = path.lastIndexOf('/');
  if (lastSlash > 0) {
    String parentPath = path.substring(0, lastSlash);
    if (!parentPath.isEmpty() && !existsLocked(parentPath)) {
      s.send(409, "text/plain", "Parent directory does not exist");
      return;
    }
  }

  if (mkdirLocked(path)) {
    s.send(201);
    LOG_DBG("DAV", "Created directory: %s", path.c_str());
  } else {
    s.send(500, "text/plain", "Failed to create directory");
  }
}

// ── MOVE ─────────────────────────────────────────────────────────────────────

void WebDAVHandler::handleMove(WebServer& s) {
  String srcPath = getRequestPath(s);
  String dstPath = getDestinationPath(s);
  bool overwrite = getOverwrite(s);

  LOG_DBG("DAV", "MOVE %s -> %s (overwrite=%d)", srcPath.c_str(), dstPath.c_str(), overwrite);

  if (srcPath == "/" || srcPath.isEmpty()) {
    s.send(403, "text/plain", "Cannot move root");
    return;
  }

  if (isProtectedPath(srcPath) || isProtectedPath(dstPath)) {
    s.send(403, "text/plain", "Forbidden");
    return;
  }

  if (dstPath.isEmpty()) {
    s.send(400, "text/plain", "Missing Destination header");
    return;
  }

  if (srcPath == dstPath) {
    s.send(204);
    return;
  }

  if (!existsLocked(srcPath)) {
    s.send(404, "text/plain", "Source not found");
    return;
  }

  // Check destination parent exists
  int lastSlash = dstPath.lastIndexOf('/');
  if (lastSlash > 0) {
    String parentPath = dstPath.substring(0, lastSlash);
    if (!parentPath.isEmpty() && !existsLocked(parentPath)) {
      s.send(409, "text/plain", "Destination parent does not exist");
      return;
    }
  }

  bool dstExists = existsLocked(dstPath);
  if (dstExists && !overwrite) {
    s.send(412, "text/plain", "Destination exists and Overwrite is F");
    return;
  }

  if (dstExists && !removeLocked(dstPath)) {
    s.send(500, "text/plain", "Failed to remove destination");
    return;
  }

  clearEpubCacheIfNeeded(srcPath);
  const bool success = renameLocked(srcPath, dstPath);

  if (success) {
    s.send(dstExists ? 204 : 201);
  } else {
    s.send(500, "text/plain", "Move failed");
  }
}

// ── COPY ─────────────────────────────────────────────────────────────────────

void WebDAVHandler::handleCopy(WebServer& s) {
  String srcPath = getRequestPath(s);
  String dstPath = getDestinationPath(s);
  bool overwrite = getOverwrite(s);

  LOG_DBG("DAV", "COPY %s -> %s (overwrite=%d)", srcPath.c_str(), dstPath.c_str(), overwrite);

  if (isProtectedPath(srcPath) || isProtectedPath(dstPath)) {
    s.send(403, "text/plain", "Forbidden");
    return;
  }

  if (dstPath.isEmpty()) {
    s.send(400, "text/plain", "Missing Destination header");
    return;
  }

  if (srcPath == dstPath) {
    s.send(204);
    return;
  }

  if (!existsLocked(srcPath)) {
    s.send(404, "text/plain", "Source not found");
    return;
  }

  FsFile srcFile = openLocked(srcPath);
  if (!srcFile) {
    s.send(500, "text/plain", "Failed to open source");
    return;
  }

  bool srcIsDirectory = false;
  {
    SpiBusMutex::Guard guard;
    srcIsDirectory = srcFile.isDirectory();
  }
  if (srcIsDirectory) {
    closeLocked(srcFile);
    s.send(403, "text/plain", "Cannot copy directories");
    return;
  }

  // Check destination parent exists
  int lastSlash = dstPath.lastIndexOf('/');
  if (lastSlash > 0) {
    String parentPath = dstPath.substring(0, lastSlash);
    if (!parentPath.isEmpty() && !existsLocked(parentPath)) {
      closeLocked(srcFile);
      s.send(409, "text/plain", "Destination parent does not exist");
      return;
    }
  }

  bool dstExists = existsLocked(dstPath);
  if (dstExists && !overwrite) {
    closeLocked(srcFile);
    s.send(412, "text/plain", "Destination exists and Overwrite is F");
    return;
  }

  if (dstExists && !removeLocked(dstPath)) {
    closeLocked(srcFile);
    s.send(500, "text/plain", "Failed to remove destination");
    return;
  }

  FsFile dstFile;
  {
    SpiBusMutex::Guard guard;
    if (!Storage.openFileForWrite("DAV", dstPath, dstFile)) {
      closeLocked(srcFile);
      s.send(500, "text/plain", "Failed to create destination");
      return;
    }
  }

  // Streaming copy with 4KB buffer on stack
  uint8_t buf[4096];
  bool copyOk = true;
  while (copyOk) {
    esp_task_wdt_reset();
    int bytesRead = 0;
    {
      SpiBusMutex::Guard guard;
      if (!srcFile.available()) {
        break;
      }
      bytesRead = srcFile.read(buf, sizeof(buf));
    }
    if (bytesRead <= 0) break;
    size_t written = 0;
    {
      SpiBusMutex::Guard guard;
      written = dstFile.write(buf, bytesRead);
    }
    if (written != (size_t)bytesRead) {
      copyOk = false;
      break;
    }
  }

  closeLocked(srcFile);
  closeLocked(dstFile);

  if (copyOk) {
    s.send(dstExists ? 204 : 201);
  } else {
    removeLocked(dstPath);
    s.send(500, "text/plain", "Copy failed - disk full?");
  }
}

// ── LOCK / UNLOCK (dummy for client compatibility) ───────────────────────────

void WebDAVHandler::handleLock(WebServer& s) {
  String path = getRequestPath(s);
  LOG_DBG("DAV", "LOCK %s (dummy)", path.c_str());

  // Return a dummy lock token for client compatibility
  String xml =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<D:prop xmlns:D=\"DAV:\">\n"
      "<D:lockdiscovery><D:activelock>\n"
      "<D:locktype><D:write/></D:locktype>\n"
      "<D:lockscope><D:exclusive/></D:lockscope>\n"
      "<D:depth>infinity</D:depth>\n"
      "<D:owner><D:href>crosspoint</D:href></D:owner>\n"
      "<D:timeout>Second-3600</D:timeout>\n"
      "<D:locktoken><D:href>urn:uuid:dummy-lock-token</D:href></D:locktoken>\n"
      "<D:lockroot><D:href>/</D:href></D:lockroot>\n"
      "</D:activelock></D:lockdiscovery>\n"
      "</D:prop>\n";

  s.sendHeader("Lock-Token", "<urn:uuid:dummy-lock-token>");
  s.send(200, "application/xml; charset=\"utf-8\"", xml);
}

void WebDAVHandler::handleUnlock(WebServer& s) {
  LOG_DBG("DAV", "UNLOCK %s (dummy)", s.uri().c_str());
  s.send(204);
}

// ── Utility functions ────────────────────────────────────────────────────────

String WebDAVHandler::getRequestPath(WebServer& s) const {
  String uri = s.uri();
  String decoded = WebServer::urlDecode(uri);

  // Normalize using FsHelpers
  std::string normalized = FsHelpers::normalisePath(decoded.c_str());
  String result = normalized.c_str();

  if (result.isEmpty()) return "/";
  if (!result.startsWith("/")) result = "/" + result;

  // Remove trailing slash unless root
  if (result.length() > 1 && result.endsWith("/")) {
    result = result.substring(0, result.length() - 1);
  }

  return result;
}

String WebDAVHandler::getDestinationPath(WebServer& s) const {
  String dest = s.header("Destination");
  if (dest.isEmpty()) return "";

  // Extract path from full URL: http://host/path -> /path
  // Find the third slash (after http://)
  int schemeEnd = dest.indexOf("://");
  if (schemeEnd >= 0) {
    int pathStart = dest.indexOf('/', schemeEnd + 3);
    if (pathStart >= 0) {
      dest = dest.substring(pathStart);
    } else {
      dest = "/";
    }
  }

  String decoded = WebServer::urlDecode(dest);
  std::string normalized = FsHelpers::normalisePath(decoded.c_str());
  String result = normalized.c_str();

  if (result.isEmpty()) return "/";
  if (!result.startsWith("/")) result = "/" + result;

  // Remove trailing slash unless root
  if (result.length() > 1 && result.endsWith("/")) {
    result = result.substring(0, result.length() - 1);
  }

  return result;
}

void WebDAVHandler::urlEncodePath(const String& path, String& out) const {
  out = "";
  for (unsigned int i = 0; i < path.length(); i++) {
    char c = path.charAt(i);
    if (c == '/') {
      out += '/';
    } else if (c == ' ') {
      out += "%20";
    } else if (c == '%') {
      out += "%25";
    } else if (c == '#') {
      out += "%23";
    } else if (c == '?') {
      out += "%3F";
    } else if (c == '&') {
      out += "%26";
    } else if ((uint8_t)c > 127) {
      // Encode non-ASCII bytes
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", (uint8_t)c);
      out += hex;
    } else {
      out += c;
    }
  }
}

bool WebDAVHandler::isProtectedPath(const String& path) const {
  // Check every segment of the path, not just the last one.
  // This prevents access to e.g. /.hidden/somefile or /System Volume Information/foo
  int start = 0;
  while (start < (int)path.length()) {
    if (path.charAt(start) == '/') {
      start++;
      continue;
    }
    int end = path.indexOf('/', start);
    if (end == -1) end = path.length();

    String segment = path.substring(start, end);

    if (segment.startsWith(".")) return true;

    for (size_t i = 0; i < HIDDEN_ITEMS_COUNT; i++) {
      if (segment.equals(HIDDEN_ITEMS[i])) return true;
    }

    start = end + 1;
  }

  return false;
}

int WebDAVHandler::getDepth(WebServer& s) const {
  String depth = s.header("Depth");
  if (depth == "0") return 0;
  if (depth == "1") return 1;
  // "infinity" or missing → treat as 1 (Class 1 servers don't need to support infinity)
  return 1;
}

bool WebDAVHandler::getOverwrite(WebServer& s) const {
  String ow = s.header("Overwrite");
  if (ow == "F" || ow == "f") return false;
  return true;  // Default is T
}

bool WebDAVHandler::existsLocked(const String& path) const {
  SpiBusMutex::Guard guard;
  return Storage.exists(path.c_str());
}

FsFile WebDAVHandler::openLocked(const String& path) const {
  SpiBusMutex::Guard guard;
  return Storage.open(path.c_str());
}

bool WebDAVHandler::removeLocked(const String& path) const {
  SpiBusMutex::Guard guard;
  return Storage.remove(path.c_str());
}

bool WebDAVHandler::renameLocked(const String& from, const String& to) const {
  SpiBusMutex::Guard guard;
  return Storage.rename(from.c_str(), to.c_str());
}

bool WebDAVHandler::mkdirLocked(const String& path) const {
  SpiBusMutex::Guard guard;
  return Storage.mkdir(path.c_str());
}

bool WebDAVHandler::rmdirLocked(const String& path) const {
  SpiBusMutex::Guard guard;
  return Storage.rmdir(path.c_str());
}

void WebDAVHandler::closeLocked(FsFile& file) const {
  SpiBusMutex::Guard guard;
  file.close();
}

bool WebDAVHandler::finalizePutTarget() {
  bool movedOriginalToBackup = false;

  if (_putExisted) {
    removeLocked(_putBackupPath);
    if (!renameLocked(_putPath, _putBackupPath)) {
      removeLocked(_putTempPath);
      return false;
    }
    movedOriginalToBackup = true;
  }

  if (!renameLocked(_putTempPath, _putPath)) {
    if (movedOriginalToBackup) {
      renameLocked(_putBackupPath, _putPath);
    }
    removeLocked(_putTempPath);
    return false;
  }

  if (movedOriginalToBackup) {
    removeLocked(_putBackupPath);
  }

  return true;
}

void WebDAVHandler::clearEpubCacheIfNeeded(const String& path) const {
  if (FsHelpers::hasEpubExtension(path)) {
    Epub(path.c_str(), "/.crosspoint").clearCache();
    LOG_DBG("DAV", "Cleared epub cache for: %s", path.c_str());
  }
}

String WebDAVHandler::getMimeType(const String& path) const {
  if (FsHelpers::hasEpubExtension(path)) return "application/epub+zip";
  if (FsHelpers::checkFileExtension(path, ".pdf")) return "application/pdf";
  if (FsHelpers::hasTxtExtension(path)) return "text/plain";
  if (FsHelpers::checkFileExtension(path, ".html") || FsHelpers::checkFileExtension(path, ".htm")) return "text/html";
  if (FsHelpers::checkFileExtension(path, ".css")) return "text/css";
  if (FsHelpers::checkFileExtension(path, ".js")) return "application/javascript";
  if (FsHelpers::checkFileExtension(path, ".json")) return "application/json";
  if (FsHelpers::checkFileExtension(path, ".xml")) return "application/xml";
  if (FsHelpers::hasJpgExtension(path)) return "image/jpeg";
  if (FsHelpers::hasPngExtension(path)) return "image/png";
  if (FsHelpers::hasGifExtension(path)) return "image/gif";
  if (FsHelpers::checkFileExtension(path, ".svg")) return "image/svg+xml";
  if (FsHelpers::checkFileExtension(path, ".zip")) return "application/zip";
  if (FsHelpers::checkFileExtension(path, ".gz")) return "application/gzip";
  return "application/octet-stream";
}

#endif  // __has_include(<NetworkUdp.h>)
