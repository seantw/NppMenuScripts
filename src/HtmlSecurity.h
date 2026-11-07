#pragma once

#include <string>

struct HtmlPermissionGrant {
    std::wstring mode; // "reconfirm" or "trust_folder"
    std::wstring folder;
    std::wstring sha256;

    bool enabled() const { return !mode.empty(); }
    bool operator==(const HtmlPermissionGrant& other) const
    {
        return mode == other.mode && folder == other.folder && sha256 == other.sha256;
    }
    bool operator!=(const HtmlPermissionGrant& other) const { return !(*this == other); }
};

// Returns a normalized absolute folder path, or an empty string on failure.
std::wstring canonicalHtmlFolderPath(const std::wstring& path);

// Computes a SHA-256 digest over every file and directory entry below folder.
// Reparse points and unreadable entries fail closed.
bool htmlFolderSha256(const std::wstring& folder, std::wstring& hexDigest);

// Confirms the saved grant still refers to this folder and, for reconfirm
// mode, that every entry and file byte is unchanged.
bool validateHtmlPermissionGrant(const std::wstring& folder,
                                 const HtmlPermissionGrant& grant);
