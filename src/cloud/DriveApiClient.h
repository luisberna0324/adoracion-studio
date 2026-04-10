#pragma once

#include <JuceHeader.h>

#include <vector>

class DriveApiClient {
public:
  struct DriveFile {
    juce::String id;
    juce::String name;
    juce::String mimeType;
    juce::String modifiedTime;
    juce::int64 size = 0;
  };

  void setAccessToken(const juce::String &accessToken);

  bool findFolderByName(const juce::String &name, const juce::String &parentId,
                        DriveFile &outFile, juce::String &errorMessage);
  bool createFolder(const juce::String &name, const juce::String &parentId,
                    DriveFile &outFile, juce::String &errorMessage);
  bool findOrCreateFolder(const juce::String &name, const juce::String &parentId,
                          DriveFile &outFile, juce::String &errorMessage);
  bool findFileByName(const juce::String &name, const juce::String &parentId,
                      DriveFile &outFile, juce::String &errorMessage);

  bool uploadTextFile(const juce::String &name, const juce::String &parentId,
                      const juce::String &mimeType, const juce::String &text,
                      juce::String &outFileId, juce::String &errorMessage,
                      const juce::String &existingFileId = {});
  bool uploadBinaryFile(const juce::String &name, const juce::String &parentId,
                        const juce::String &mimeType, const juce::File &file,
                        juce::String &outFileId, juce::String &errorMessage,
                        const juce::String &existingFileId = {});

  bool downloadFileToString(const juce::String &fileId, juce::String &outText,
                            juce::String &errorMessage);
  bool downloadFileToFile(const juce::String &fileId,
                          const juce::File &destination,
                          juce::String &errorMessage);

private:
  bool requestJson(const juce::URL &url, const juce::String &method,
                   const juce::MemoryBlock *body, const juce::String &contentType,
                   juce::var &outJson, juce::String &outRaw,
                   juce::String &errorMessage);
  bool requestBytes(const juce::URL &url, const juce::String &method,
                    const juce::MemoryBlock *body, const juce::String &contentType,
                    juce::MemoryBlock &outBytes, juce::String &errorMessage);
  juce::String authHeaders(const juce::String &contentType) const;
  static juce::String escapeQueryValue(const juce::String &value);
  static DriveFile fileFromVar(const juce::var &itemVar);
  bool uploadMultipart(const juce::String &name, const juce::String &parentId,
                       const juce::String &mimeType, const juce::MemoryBlock &content,
                       juce::String &outFileId, juce::String &errorMessage,
                       const juce::String &existingFileId);

  juce::String token;
};
