// Minimal ATL subset for building upstream WinDirStat with llvm-mingw (spike).
#pragma once
#include <windows.h>
#include <oleauto.h>
#include <objbase.h>
#include <utility>
#include <string>

template <class T> class CComPtr
{
public:
    T* p = nullptr;
    CComPtr() noexcept = default;
    CComPtr(T* lp) noexcept : p(lp) { if (p) p->AddRef(); }
    CComPtr(const CComPtr& o) noexcept : CComPtr(o.p) {}
    CComPtr(CComPtr&& o) noexcept : p(o.p) { o.p = nullptr; }
    template <class U> CComPtr(const CComPtr<U>& o) noexcept : CComPtr(static_cast<T*>(o.p)) {}
    ~CComPtr() { if (p) p->Release(); }
    CComPtr& operator=(T* lp) noexcept { if (lp) lp->AddRef(); if (p) p->Release(); p = lp; return *this; }
    CComPtr& operator=(const CComPtr& o) noexcept { return *this = o.p; }
    CComPtr& operator=(CComPtr&& o) noexcept { if (this != &o) { if (p) p->Release(); p = o.p; o.p = nullptr; } return *this; }
    operator T*() const noexcept { return p; }
    T& operator*() const { return *p; }
    T** operator&() noexcept { return &p; }
    T* operator->() const noexcept { return p; }
    bool operator!() const noexcept { return p == nullptr; }
    void Release() noexcept { if (p) { T* t = p; p = nullptr; t->Release(); } }
    void Attach(T* lp) noexcept { if (p) p->Release(); p = lp; }
    T* Detach() noexcept { T* t = p; p = nullptr; return t; }
    HRESULT CopyTo(T** pp) const noexcept { if (!pp) return E_POINTER; *pp = p; if (p) p->AddRef(); return S_OK; }
    HRESULT CoCreateInstance(REFCLSID clsid, IUnknown* outer = nullptr, DWORD ctx = CLSCTX_ALL) noexcept
    { return ::CoCreateInstance(clsid, outer, ctx, __uuidof(T), reinterpret_cast<void**>(&p)); }
    template <class Q> HRESULT QueryInterface(Q** pp) const noexcept { return p->QueryInterface(__uuidof(Q), reinterpret_cast<void**>(pp)); }
};

template <class T, const IID* piid = &__uuidof(T)> class CComQIPtr : public CComPtr<T>
{
public:
    CComQIPtr() noexcept = default;
    CComQIPtr(T* lp) noexcept : CComPtr<T>(lp) {}
    CComQIPtr(IUnknown* lp) noexcept { if (lp) lp->QueryInterface(*piid, reinterpret_cast<void**>(&this->p)); }
    template <class U> CComQIPtr(const CComPtr<U>& o) noexcept : CComQIPtr(static_cast<IUnknown*>(o.p)) {}
};

template <class T> class CComHeapPtr
{
public:
    T* m_pData = nullptr;
    ~CComHeapPtr() { Free(); }
    void Free() noexcept { CoTaskMemFree(m_pData); m_pData = nullptr; }
    operator T*() const noexcept { return m_pData; }
    T** operator&() noexcept { return &m_pData; }
    T* operator->() const noexcept { return m_pData; }
    bool Allocate(size_t n = 1) noexcept { Free(); m_pData = static_cast<T*>(CoTaskMemAlloc(n * sizeof(T))); return m_pData != nullptr; }
    void Attach(T* d) noexcept { Free(); m_pData = d; }
    T* Detach() noexcept { T* d = m_pData; m_pData = nullptr; return d; }
};

class CComBSTR
{
public:
    BSTR m_str = nullptr;
    CComBSTR() noexcept = default;
    CComBSTR(LPCOLESTR s) : m_str(s ? SysAllocString(s) : nullptr) {}
    CComBSTR(int len, LPCOLESTR s) : m_str(SysAllocStringLen(s, static_cast<UINT>(len))) {}
    CComBSTR(const std::wstring& s) : m_str(SysAllocStringLen(s.data(), static_cast<UINT>(s.size()))) {}
    CComBSTR(const CComBSTR& o) : m_str(o.m_str ? SysAllocStringByteLen(reinterpret_cast<char*>(o.m_str), SysStringByteLen(o.m_str)) : nullptr) {}
    ~CComBSTR() { SysFreeString(m_str); }
    CComBSTR& operator=(LPCOLESTR s) { SysFreeString(m_str); m_str = s ? SysAllocString(s) : nullptr; return *this; }
    operator BSTR() const noexcept { return m_str; }
    BSTR* operator&() noexcept { return &m_str; }
    unsigned Length() const noexcept { return m_str ? SysStringLen(m_str) : 0; }
    void Empty() noexcept { SysFreeString(m_str); m_str = nullptr; }
    BSTR Detach() noexcept { BSTR s = m_str; m_str = nullptr; return s; }
    bool operator!() const noexcept { return m_str == nullptr; }
};

class CComVariant : public tagVARIANT
{
public:
    CComVariant() noexcept { VariantInit(this); }
    CComVariant(LPCOLESTR s) { VariantInit(this); vt = VT_BSTR; bstrVal = SysAllocString(s); }
    CComVariant(bool b) noexcept { VariantInit(this); vt = VT_BOOL; boolVal = b ? VARIANT_TRUE : VARIANT_FALSE; }
    CComVariant(int v) noexcept { VariantInit(this); vt = VT_I4; lVal = v; }
    CComVariant(LONG v) noexcept { VariantInit(this); vt = VT_I4; lVal = v; }
    CComVariant(ULONG v) noexcept { VariantInit(this); vt = VT_UI4; ulVal = v; }
    CComVariant(const CComVariant& o) { VariantInit(this); VariantCopy(this, const_cast<CComVariant*>(&o)); }
    ~CComVariant() { VariantClear(this); }
    CComVariant& operator=(const CComVariant& o) { if (this != &o) VariantCopy(this, const_cast<CComVariant*>(&o)); return *this; }
    HRESULT Clear() noexcept { return VariantClear(this); }
    HRESULT ChangeType(VARTYPE t, const VARIANT* src = nullptr) { return VariantChangeType(this, src ? src : this, 0, t); }
};

class CRegKey
{
public:
    HKEY m_hKey = nullptr;
    CRegKey() noexcept = default;
    CRegKey(const CRegKey&) = delete;
    ~CRegKey() { Close(); }
    operator HKEY() const noexcept { return m_hKey; }
    LONG Close() noexcept { LONG r = ERROR_SUCCESS; if (m_hKey) { r = RegCloseKey(m_hKey); m_hKey = nullptr; } return r; }
    LONG Open(HKEY parent, LPCWSTR name, REGSAM sam = KEY_READ | KEY_WRITE) noexcept
    { Close(); return RegOpenKeyExW(parent, name, 0, sam, &m_hKey); }
    LONG Create(HKEY parent, LPCWSTR name, LPWSTR cls = nullptr, DWORD opts = REG_OPTION_NON_VOLATILE,
        REGSAM sam = KEY_READ | KEY_WRITE, LPSECURITY_ATTRIBUTES sa = nullptr, LPDWORD disp = nullptr) noexcept
    { Close(); return RegCreateKeyExW(parent, name, 0, cls, opts, sam, sa, &m_hKey, disp); }
    LONG QueryDWORDValue(LPCWSTR n, DWORD& v) noexcept
    { DWORD sz = sizeof v, t = 0; LONG r = RegQueryValueExW(m_hKey, n, nullptr, &t, reinterpret_cast<BYTE*>(&v), &sz); return r == ERROR_SUCCESS && t != REG_DWORD ? ERROR_INVALID_DATA : r; }
    LONG SetDWORDValue(LPCWSTR n, DWORD v) noexcept
    { return RegSetValueExW(m_hKey, n, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof v); }
    LONG QueryStringValue(LPCWSTR n, LPWSTR buf, ULONG* chars) noexcept
    { DWORD sz = *chars * sizeof(wchar_t), t = 0; LONG r = RegQueryValueExW(m_hKey, n, nullptr, &t, reinterpret_cast<BYTE*>(buf), &sz); *chars = sz / sizeof(wchar_t); return r; }
    LONG SetStringValue(LPCWSTR n, LPCWSTR v, DWORD type = REG_SZ) noexcept
    { return RegSetValueExW(m_hKey, n, 0, type, reinterpret_cast<const BYTE*>(v), static_cast<DWORD>((wcslen(v) + 1) * sizeof(wchar_t))); }
    LONG QueryBinaryValue(LPCWSTR n, void* buf, ULONG* bytes) noexcept
    { DWORD sz = *bytes, t = 0; LONG r = RegQueryValueExW(m_hKey, n, nullptr, &t, static_cast<BYTE*>(buf), &sz); *bytes = sz; return r; }
    LONG SetBinaryValue(LPCWSTR n, const void* v, ULONG bytes) noexcept
    { return RegSetValueExW(m_hKey, n, 0, REG_BINARY, static_cast<const BYTE*>(v), bytes); }
    LONG QueryValue(LPCWSTR n, DWORD* type, void* data, ULONG* bytes) noexcept
    { DWORD sz = *bytes; LONG r = RegQueryValueExW(m_hKey, n, nullptr, type, static_cast<BYTE*>(data), &sz); *bytes = sz; return r; }
    LONG DeleteValue(LPCWSTR n) noexcept { return RegDeleteValueW(m_hKey, n); }
    LONG DeleteSubKey(LPCWSTR n) noexcept { return RegDeleteKeyW(m_hKey, n); }
    LONG RecurseDeleteKey(LPCWSTR n) noexcept { return RegDeleteTreeW(m_hKey, n); }
    void Attach(HKEY k) noexcept { Close(); m_hKey = k; }
    HKEY Detach() noexcept { HKEY k = m_hKey; m_hKey = nullptr; return k; }
};
