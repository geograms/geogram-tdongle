#include <WebServer.h>
#include "drive/storage.h"

extern WebServer server;
extern StorageManager storage;

void handleFileList() {
    String dir = server.arg("dir");
    if (dir.length() == 0) dir = "/";
    fs::FS& fs = storage.getActiveFS();

    File root = fs.open(dir);
    if (!root || !root.isDirectory()) {
        server.send(400, "application/json", "{\"error\":\"Invalid path\"}");
        return;
    }

    File file = root.openNextFile();
    String output = "[";
    bool first = true;

    while (file) {
        if (!first) output += ",";
        first = false;

        String fullPath = String(file.name());
        String trimmed = fullPath;
        if (dir != "/" && fullPath.startsWith(dir)) {
            trimmed = fullPath.substring(dir.length());
        }

        output += "{\"name\":\"" + trimmed + "\",";
        output += "\"size\":" + String(file.size()) + "}";

        file = root.openNextFile();
    }

    output += "]";
    server.send(200, "application/json", output);
}

void handleFileUpload() {
    static File f;
    fs::FS& fs = storage.getActiveFS();
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        f = fs.open("/" + upload.filename, "w");
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (f) f.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
        if (f) f.close();
    }
}

void handleFileDownload() {
    String name = server.arg("name");
    fs::FS& fs = storage.getActiveFS();

    if (!fs.exists(name)) {
        server.send(404, "text/plain", "Not found");
        return;
    }

    File f = fs.open(name, "r");
    server.streamFile(f, "application/octet-stream");
    f.close();
}

void handleFileDelete() {
    String name = server.arg("name");
    fs::FS& fs = storage.getActiveFS();

    if (fs.exists(name)) {
        fs.remove(name);
    }
    server.send(200, "text/plain", "OK");
}

void handleFileRename() {
    String oldName = server.arg("old");
    String newName = server.arg("new");
    fs::FS& fs = storage.getActiveFS();

    if (fs.exists(oldName)) {
        fs.rename(oldName, newName);
        server.send(200, "text/plain", "OK");
    } else {
        server.send(404, "text/plain", "Not found");
    }
}

void setupFileBrowserRoutes() {
    server.on("/api/files/list", HTTP_GET, handleFileList);
    server.on("/api/files/read", HTTP_GET, handleFileDownload);
    server.on("/api/files/delete", HTTP_GET, handleFileDelete);
    server.on("/api/files/rename", HTTP_POST, handleFileRename);
    server.on("/api/files/upload", HTTP_POST, []() { server.send(200); }, handleFileUpload);
}
