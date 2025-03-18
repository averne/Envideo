/*
 * Copyright (c) 2025 averne <averne381@gmail.com>
 *
 * This file is part of Envideo.

 * Envideo is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License,
 * or (at your option) any later version.

 * Envideo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with Envideo. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <algorithm>
#include <utility>
#include <vector>

namespace envid::util {

#define _ENVID_CAT(x, y) x ## y
#define  ENVID_CAT(x, y) _ENVID_CAT(x, y)
#define _ENVID_STR(x) #x
#define  ENVID_STR(x) _ENVID_STR(x)

#define ENVID_ANONYMOUS ENVID_CAT(var, __COUNTER__)

#define ENVID_SCOPEGUARD(f) auto ENVID_ANONYMOUS = ::envid::util::ScopeGuard(f)

#define ENVID_UNUSED(...) ::envid::util::unused(__VA_ARGS__)

#define ENVID_CHECK(expr) ({        \
    if (auto _rc_ = (expr); _rc_)   \
        return _rc_;                \
})

#define ENVID_CHECK_ERRNO(expr) ({       \
    if (auto rc = (expr); rc < 0)        \
        return ENVIDEO_RC_SYSTEM(errno); \
})

#define ENVID_CHECK_RC(expr) ({          \
    if (auto rc = (expr); R_FAILED(rc))  \
        return ENVIDEO_RC_SYSTEM(rc);    \
})

#define ENVID_CHECK_RM(expr, status) ({  \
    if (auto rc = (expr); rc < 0)        \
        return ENVIDEO_RC_SYSTEM(errno); \
    else if (status)                     \
        return ENVIDEO_RC_RM(status);    \
})

void unused(auto &&...args) {
    (static_cast<void>(args), ...);
}

constexpr auto align_down(auto v, auto a) {
    return v & ~(a - 1);
}

constexpr auto align_up(auto v, auto a) {
    return align_down(v + a - 1, a);
}

constexpr auto bit(auto bit) {
    return static_cast<decltype(bit)>(1) << bit;
}

constexpr auto mask(auto bit) {
    return (static_cast<decltype(bit)>(1) << bit) - 1;
}

static inline void write_fence() {
#if defined(__amd64__) || defined(_M_AMD64)
    asm volatile("sfence" ::: "memory");
#elif defined(__aarch64__) || defined(_M_ARM64)
    asm volatile("dsb st" ::: "memory");
#else
#error "Unsupported CPU architecture"
#endif
}

template <typename F>
struct ScopeGuard {
    [[nodiscard]] ScopeGuard(F &&f): f(std::move(f)) { }

    ScopeGuard(const ScopeGuard &) = delete;
    ScopeGuard &operator =(const ScopeGuard &) = delete;

    ~ScopeGuard() {
        if (this->want_run)
            this->f();
    }

    void cancel() {
        this->want_run = false;
    }

    private:
        bool want_run = true;
        F f;
};

template <typename K, typename V>
class FlatHashMap {
    private:
        using P = std::pair<K, V>;

    public:
        constexpr bool insert(const K &k, const V &v) {
            auto it = this->find_pair(k);
            return (it != this->data.end() && it->first == k) ? false : (this->data.insert(it, { k, v }), true);
        }

        constexpr bool erase(const K &k) {
            auto it = this->find_pair(k);
            return (it != this->data.end() && it->first == k) ? (this->data.erase(it), true) : false;
        }

        constexpr bool contains(const K &k) {
            auto it = this->find_pair(k);
            return (it != this->data.end() && it->first == k);
        }

        constexpr V *find(const K &k) {
            auto it = this->find_pair(k);
            return (it != this->data.end() && it->first == k) ? &it->second : nullptr;
        }

        constexpr V &operator [](const K& k) {
            auto it = this->find_pair(k);
            if (it == data.end() || it->first != k)
                it = data.insert(it, { k, {} });
            return it->second;
        }

        constexpr std::size_t size() const {
            return this->data.size();
        }

    private:
        constexpr auto find_pair(const K &k) {
            return std::ranges::lower_bound(this->data, k, {}, &P::first);
        }

        std::vector<P> data = {};
};

static_assert([] { FlatHashMap<int, int> hm;
    hm.insert(1, 2), hm.insert(3, 4), hm.insert(5, 6); hm.erase(3);
    return hm.size() == 2;
}());

static_assert([] { FlatHashMap<int, int> hm;
    hm.insert(1, 2), hm.insert(3, 4), hm.insert(5, 6);
    return hm.contains(3) && *hm.find(3) == 4 && hm.find(4) == nullptr;
}());

static_assert([] { FlatHashMap<int, int> hm;
    hm.insert(1, 2), hm.insert(3, 4), hm.insert(5, 6), hm.erase(3);
    return hm.size() == 2 && hm.find(3) == nullptr;
}());

static_assert([] { FlatHashMap<int, int> hm;
    hm[1] = 2, hm[3] = 4, hm.erase(3);
    return hm.size() == 1 && hm[1] == 2 && hm.find(3) == nullptr && hm[10] == 0;
}());

} // namespace envid::util
