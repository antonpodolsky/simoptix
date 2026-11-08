// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// UniqueHandle and MappedView: owners of a Win32 handle and of a mapped view of a file mapping. Each
// closes what it holds when it goes out of scope, so a function with early returns, or a class whose
// start failed halfway, leaks nothing. They move and never copy. Knows nothing about what the handle is
// for (an event, a file mapping, a process, a timer).
#pragma once

#include <windows.h>

#include <type_traits>

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) { Reset(handle); }
    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.Release()) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            Reset(other.Release());
        }
        return *this;
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    ~UniqueHandle() { Reset(); }

    HANDLE Get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

    // Closes what it holds and takes `handle` (nullptr, or what a failed CreateFile returns, is nothing).
    void Reset(HANDLE handle = nullptr) {
        if (handle_) {
            CloseHandle(handle_);
        }
        handle_ = handle == INVALID_HANDLE_VALUE ? nullptr : handle;
    }

    HANDLE Release() {
        HANDLE handle = handle_;
        handle_ = nullptr;
        return handle;
    }

    // For a Win32 call that writes a handle (DuplicateHandle): the old one is closed first.
    HANDLE* Receive() {
        Reset();
        return &handle_;
    }

private:
    HANDLE handle_ = nullptr;
};

template <class T>
class MappedView {
public:
    MappedView() = default;
    explicit MappedView(T* view) : view_(view) {}
    MappedView(MappedView&& other) noexcept : view_(other.Release()) {}
    MappedView& operator=(MappedView&& other) noexcept {
        if (this != &other) {
            Reset(other.Release());
        }
        return *this;
    }

    MappedView(const MappedView&) = delete;
    MappedView& operator=(const MappedView&) = delete;
    ~MappedView() { Reset(); }

    T* Get() const { return view_; }
    T* operator->() const { return view_; }
    std::add_lvalue_reference_t<T> operator*() const { return *view_; }
    explicit operator bool() const { return view_ != nullptr; }

    void Reset(T* view = nullptr) {
        if (view_) {
            UnmapViewOfFile(view_);
        }
        view_ = view;
    }

    T* Release() {
        T* view = view_;
        view_ = nullptr;
        return view;
    }

private:
    T* view_ = nullptr;
};
