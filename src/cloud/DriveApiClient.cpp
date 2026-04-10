#include "DriveApiClient.h"

#include <cstring>

namespace {
constexpr const char *kDriveFilesApi = "https://www.googleapis.com/drive/v3/files";
constexpr const char *kDriveUploadApi =
    "https://www.googleapis.com/upload/drive/v3/files";
constexpr const char *kFolderMimeType = "application/vnd.google-apps.folder";

juce::MemoryBlock stringToUtf8Block(const juce::String &text) {
  const auto utf8 = text.toRawUTF8();
  const auto bytes = static_cast<size_t>(std::strlen(utf8));
  juce::MemoryBlock block(utf8, bytes);
  return block;
}
} // namespace

void DriveApiClient::setAccessToken(const juce::String &accessToken) {
  token = accessToken.trim();
}

bool DriveApiClient::findFolderByName(const juce::String &name,
                                      const juce::String &parentId,
                                      DriveFile &outFile,
                                      juce::String &errorMessage) {
  const auto q =
      "trashed=false and mimeType='" + juce::String(kFolderMimeType) +
      "' and name='" + escapeQueryValue(name) + "' and '" +
      escapeQueryValue(parentId) + "' in parents";
  juce::URL url(kDriveFilesApi);
  url = url.withParameter("q", q)
            .withParameter("pageSize", "1")
            .withParameter("fields",
                           "files(id,name,mimeType,modifiedTime,size)");
  juce::var json;
  juce::String raw;
  if (!requestJson(url, "GET", nullptr, {}, json, raw, errorMessage))
    return false;
  if (auto *obj = json.getDynamicObject()) {
    if (auto *files = obj->getProperty("files").getArray()) {
      if (!files->isEmpty()) {
        outFile = fileFromVar(files->getFirst());
        return outFile.id.isNotEmpty();
      }
    }
  }
  return false;
}

bool DriveApiClient::createFolder(const juce::String &name,
                                  const juce::String &parentId,
                                  DriveFile &outFile,
                                  juce::String &errorMessage) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("name", name);
  obj->setProperty("mimeType", kFolderMimeType);
  juce::Array<juce::var> parents;
  if (parentId.isNotEmpty())
    parents.add(parentId);
  obj->setProperty("parents", juce::var(parents));

  const auto body = stringToUtf8Block(juce::JSON::toString(juce::var(obj), false));
  juce::URL url(kDriveFilesApi);
  url = url.withParameter("fields", "id,name,mimeType,modifiedTime,size");
  juce::var json;
  juce::String raw;
  if (!requestJson(url, "POST", &body, "application/json; charset=utf-8", json,
                   raw, errorMessage))
    return false;
  outFile = fileFromVar(json);
  return outFile.id.isNotEmpty();
}

bool DriveApiClient::findOrCreateFolder(const juce::String &name,
                                        const juce::String &parentId,
                                        DriveFile &outFile,
                                        juce::String &errorMessage) {
  if (findFolderByName(name, parentId, outFile, errorMessage))
    return true;
  return createFolder(name, parentId, outFile, errorMessage);
}

bool DriveApiClient::findFileByName(const juce::String &name,
                                    const juce::String &parentId,
                                    DriveFile &outFile,
                                    juce::String &errorMessage) {
  const auto q = "trashed=false and name='" + escapeQueryValue(name) +
                 "' and '" + escapeQueryValue(parentId) + "' in parents";
  juce::URL url(kDriveFilesApi);
  url = url.withParameter("q", q)
            .withParameter("pageSize", "1")
            .withParameter("fields",
                           "files(id,name,mimeType,modifiedTime,size)");
  juce::var json;
  juce::String raw;
  if (!requestJson(url, "GET", nullptr, {}, json, raw, errorMessage))
    return false;
  if (auto *obj = json.getDynamicObject()) {
    if (auto *files = obj->getProperty("files").getArray()) {
      if (!files->isEmpty()) {
        outFile = fileFromVar(files->getFirst());
        return outFile.id.isNotEmpty();
      }
    }
  }
  return false;
}

bool DriveApiClient::uploadTextFile(const juce::String &name,
                                    const juce::String &parentId,
                                    const juce::String &mimeType,
                                    const juce::String &text,
                                    juce::String &outFileId,
                                    juce::String &errorMessage,
                                    const juce::String &existingFileId) {
  const auto body = stringToUtf8Block(text);
  return uploadMultipart(name, parentId, mimeType, body, outFileId,
                         errorMessage, existingFileId);
}

bool DriveApiClient::uploadBinaryFile(const juce::String &name,
                                      const juce::String &parentId,
                                      const juce::String &mimeType,
                                      const juce::File &file,
                                      juce::String &outFileId,
                                      juce::String &errorMessage,
                                      const juce::String &existingFileId) {
  if (!file.existsAsFile()) {
    errorMessage = "File not found: " + file.getFullPathName();
    return false;
  }
  juce::MemoryBlock body;
  if (!file.loadFileAsData(body)) {
    errorMessage = "Couldn't read file for upload: " + file.getFullPathName();
    return false;
  }
  return uploadMultipart(name, parentId, mimeType, body, outFileId,
                         errorMessage, existingFileId);
}

bool DriveApiClient::downloadFileToString(const juce::String &fileId,
                                          juce::String &outText,
                                          juce::String &errorMessage) {
  juce::URL url(juce::String(kDriveFilesApi) + "/" + fileId);
  url = url.withParameter("alt", "media");
  juce::MemoryBlock bytes;
  if (!requestBytes(url, "GET", nullptr, {}, bytes, errorMessage))
    return false;
  outText = juce::String::fromUTF8(static_cast<const char *>(bytes.getData()),
                                   static_cast<int>(bytes.getSize()));
  return true;
}

bool DriveApiClient::downloadFileToFile(const juce::String &fileId,
                                        const juce::File &destination,
                                        juce::String &errorMessage) {
  juce::URL url(juce::String(kDriveFilesApi) + "/" + fileId);
  url = url.withParameter("alt", "media");
  juce::MemoryBlock bytes;
  if (!requestBytes(url, "GET", nullptr, {}, bytes, errorMessage))
    return false;
  destination.getParentDirectory().createDirectory();
  return destination.replaceWithData(bytes.getData(), bytes.getSize());
}

bool DriveApiClient::requestJson(const juce::URL &url, const juce::String &method,
                                 const juce::MemoryBlock *body,
                                 const juce::String &contentType, juce::var &outJson,
                                 juce::String &outRaw, juce::String &errorMessage) {
  juce::MemoryBlock outBytes;
  if (!requestBytes(url, method, body, contentType, outBytes, errorMessage))
    return false;
  outRaw = juce::String::fromUTF8(static_cast<const char *>(outBytes.getData()),
                                  static_cast<int>(outBytes.getSize()));
  if (outRaw.isEmpty()) {
    outJson = juce::var();
    return true;
  }
  auto parsed = juce::JSON::parse(outRaw);
  if (parsed.isVoid()) {
    errorMessage = "Invalid JSON response from Drive.";
    return false;
  }
  outJson = parsed;
  return true;
}

bool DriveApiClient::requestBytes(const juce::URL &url, const juce::String &method,
                                  const juce::MemoryBlock *body,
                                  const juce::String &contentType,
                                  juce::MemoryBlock &outBytes,
                                  juce::String &errorMessage) {
  juce::StringPairArray responseHeaders;
  int status = 0;
  auto requestUrl = url;
  if (body != nullptr)
    requestUrl = requestUrl.withPOSTData(*body);

  auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                     .withHttpRequestCmd(method)
                     .withConnectionTimeoutMs(20000)
                     .withResponseHeaders(&responseHeaders)
                     .withStatusCode(&status)
                     .withExtraHeaders(authHeaders(contentType));

  auto stream = requestUrl.createInputStream(options);
  if (stream == nullptr) {
    errorMessage = "Couldn't connect to Google Drive API.";
    return false;
  }

  juce::MemoryOutputStream responseStream;
  responseStream.writeFromInputStream(*stream, -1);
  outBytes = responseStream.getMemoryBlock();
  const auto responseText = juce::String::fromUTF8(
      static_cast<const char *>(outBytes.getData()),
      static_cast<int>(outBytes.getSize()));

  if (status < 200 || status >= 300) {
    errorMessage = "Drive API request failed (" + juce::String(status) + "): " +
                   responseText;
    return false;
  }
  return true;
}

juce::String DriveApiClient::authHeaders(const juce::String &contentType) const {
  juce::String headers;
  headers << "Authorization: Bearer " << token << "\r\n";
  if (contentType.isNotEmpty())
    headers << "Content-Type: " << contentType << "\r\n";
  return headers;
}

juce::String DriveApiClient::escapeQueryValue(const juce::String &value) {
  return value.replace("'", "\\'");
}

DriveApiClient::DriveFile DriveApiClient::fileFromVar(const juce::var &itemVar) {
  DriveFile file;
  const auto *obj = itemVar.getDynamicObject();
  if (obj == nullptr)
    return file;
  file.id = obj->getProperty("id").toString();
  file.name = obj->getProperty("name").toString();
  file.mimeType = obj->getProperty("mimeType").toString();
  file.modifiedTime = obj->getProperty("modifiedTime").toString();
  if (obj->hasProperty("size"))
    file.size = obj->getProperty("size").toString().getLargeIntValue();
  return file;
}

bool DriveApiClient::uploadMultipart(const juce::String &name,
                                     const juce::String &parentId,
                                     const juce::String &mimeType,
                                     const juce::MemoryBlock &content,
                                     juce::String &outFileId,
                                     juce::String &errorMessage,
                                     const juce::String &existingFileId) {
  const auto boundary =
      "pw_boundary_" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64());

  auto *metaObj = new juce::DynamicObject();
  metaObj->setProperty("name", name);
  if (parentId.isNotEmpty()) {
    juce::Array<juce::var> parents;
    parents.add(parentId);
    metaObj->setProperty("parents", juce::var(parents));
  }
  const auto metadata = juce::JSON::toString(juce::var(metaObj), false);

  juce::MemoryOutputStream body;
  body << "--" << boundary << "\r\n";
  body << "Content-Type: application/json; charset=UTF-8\r\n\r\n";
  body << metadata << "\r\n";
  body << "--" << boundary << "\r\n";
  body << "Content-Type: " << mimeType << "\r\n\r\n";
  body.write(content.getData(), content.getSize());
  body << "\r\n--" << boundary << "--\r\n";

  const auto contentType =
      "multipart/related; boundary=" + boundary;
  juce::URL url(existingFileId.isNotEmpty()
                    ? juce::String(kDriveUploadApi) + "/" + existingFileId
                    : juce::String(kDriveUploadApi));
  url = url.withParameter("uploadType", "multipart")
            .withParameter("fields", "id");

  juce::var json;
  juce::String raw;
  const auto multipartBody = body.getMemoryBlock();
  if (!requestJson(url, existingFileId.isNotEmpty() ? "PATCH" : "POST",
                   &multipartBody, contentType, json, raw, errorMessage))
    return false;

  const auto *obj = json.getDynamicObject();
  if (obj == nullptr) {
    errorMessage = "Invalid upload response from Drive.";
    return false;
  }
  outFileId = obj->getProperty("id").toString();
  if (outFileId.isEmpty()) {
    errorMessage = "Drive upload did not return file id.";
    return false;
  }
  return true;
}
