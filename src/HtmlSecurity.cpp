#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "HtmlSecurity.h"

namespace {
class Sha256 {
public:
    Sha256()
    {
        if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            return;
        DWORD objectSize = 0, bytes = 0;
        if (BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &bytes, 0) < 0 ||
            BCryptGetProperty(algorithm_, BCRYPT_HASH_LENGTH,
                reinterpret_cast<PUCHAR>(&digestSize_), sizeof(digestSize_), &bytes, 0) < 0 ||
            digestSize_ != digest_.size()) return;
        object_.resize(objectSize);
        if (BCryptCreateHash(algorithm_, &hash_, object_.data(), objectSize, nullptr, 0, 0) < 0)
            hash_ = nullptr;
    }
    ~Sha256()
    {
        if (hash_) BCryptDestroyHash(hash_);
        if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
    }
    bool good() const { return hash_ != nullptr; }
    bool update(const void* data, size_t size)
    {
        const auto* bytes = static_cast<const UCHAR*>(data);
        while (size) {
            const ULONG chunk = static_cast<ULONG>((std::min)(size, static_cast<size_t>(0x7fffffff)));
            if (BCryptHashData(hash_, const_cast<PUCHAR>(bytes), chunk, 0) < 0) return false;
            bytes += chunk;
            size -= chunk;
        }
        return true;
    }
    bool finish(std::array<UCHAR, 32>& output)
    {
        return hash_ && BCryptFinishHash(hash_, output.data(), static_cast<ULONG>(output.size()), 0) >= 0;
    }
private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    DWORD digestSize_ = 0;
    std::vector<UCHAR> object_;
    std::array<UCHAR, 32> digest_{};
};

bool addRecord(Sha256& hash, wchar_t kind, const std::wstring& relative, ULONGLONG size)
{
    const DWORD chars = static_cast<DWORD>(relative.size());
    return hash.update(&kind, sizeof(kind)) && hash.update(&chars, sizeof(chars)) &&
        hash.update(relative.data(), relative.size() * sizeof(wchar_t)) &&
        hash.update(&size, sizeof(size));
}

bool hashDirectory(Sha256& hash, const std::wstring& root, const std::wstring& relative)
{
    const std::wstring directory = relative.empty() ? root : root + L"\\" + relative;
    WIN32_FIND_DATAW data{};
    HANDLE search = FindFirstFileW((directory + L"\\*").c_str(), &data);
    if (search == INVALID_HANDLE_VALUE) return false;

    std::vector<WIN32_FIND_DATAW> entries;
    do {
        if (wcscmp(data.cFileName, L".") != 0 && wcscmp(data.cFileName, L"..") != 0)
            entries.push_back(data);
    } while (FindNextFileW(search, &data));
    const DWORD findError = GetLastError();
    FindClose(search);
    if (findError != ERROR_NO_MORE_FILES) return false;

    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return CompareStringOrdinal(a.cFileName, -1, b.cFileName, -1, TRUE) == CSTR_LESS_THAN;
    });

    for (const auto& entry : entries) {
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return false;
        const std::wstring child = relative.empty() ? entry.cFileName : relative + L"\\" + entry.cFileName;
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!addRecord(hash, L'D', child, 0) || !hashDirectory(hash, root, child)) return false;
            continue;
        }

        const ULONGLONG expectedSize = (static_cast<ULONGLONG>(entry.nFileSizeHigh) << 32) | entry.nFileSizeLow;
        if (!addRecord(hash, L'F', child, expectedSize)) return false;
        HANDLE file = CreateFileW((root + L"\\" + child).c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        std::array<UCHAR, 64 * 1024> buffer{};
        ULONGLONG total = 0;
        bool ok = true;
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
                ok = false;
                break;
            }
            if (!read) break;
            total += read;
            if (!hash.update(buffer.data(), read)) { ok = false; break; }
        }
        CloseHandle(file);
        if (!ok || total != expectedSize) return false;
    }
    return true;
}
}

std::wstring canonicalHtmlFolderPath(const std::wstring& path)
{
    if (path.empty()) return {};
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (!length || length >= buffer.size()) return {};
    std::wstring result(buffer.data(), length);
    while (result.size() > 3 && (result.back() == L'\\' || result.back() == L'/')) result.pop_back();
    const DWORD attributes = GetFileAttributesW(result.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return {};
    return result;
}

bool htmlFolderSha256(const std::wstring& folder, std::wstring& hexDigest)
{
    hexDigest.clear();
    const std::wstring root = canonicalHtmlFolderPath(folder);
    if (root.empty()) return false;
    const DWORD attrs = GetFileAttributesW((root + L"\\index.html").c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) ||
        (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return false;

    Sha256 hash;
    if (!hash.good() || !addRecord(hash, L'R', L"", 0) || !hashDirectory(hash, root, L"")) return false;
    std::array<UCHAR, 32> digest{};
    if (!hash.finish(digest)) return false;
    static constexpr wchar_t digits[] = L"0123456789abcdef";
    hexDigest.reserve(digest.size() * 2);
    for (UCHAR byte : digest) {
        hexDigest.push_back(digits[byte >> 4]);
        hexDigest.push_back(digits[byte & 0x0f]);
    }
    return true;
}

bool validateHtmlPermissionGrant(const std::wstring& folder,
                                 const HtmlPermissionGrant& grant)
{
    const std::wstring currentFolder = canonicalHtmlFolderPath(folder);
    const std::wstring authorizedFolder = canonicalHtmlFolderPath(grant.folder);
    if (currentFolder.empty() || authorizedFolder.empty() ||
        CompareStringOrdinal(currentFolder.c_str(), -1, authorizedFolder.c_str(), -1, TRUE) != CSTR_EQUAL)
        return false;
    if (grant.mode == L"trust_folder") return grant.sha256.empty();
    if (grant.mode != L"reconfirm" || grant.sha256.size() != 64) return false;
    std::wstring currentHash;
    return htmlFolderSha256(currentFolder, currentHash) &&
        CompareStringOrdinal(currentHash.c_str(), -1, grant.sha256.c_str(), -1, TRUE) == CSTR_EQUAL;
}
