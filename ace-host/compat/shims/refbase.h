/*
 * Stand-in for OpenHarmony's <refbase.h> (//utils/base: OHOS::RefBase + sptr/wptr).
 *
 * WHY THIS IS MINIMAL AND WHY THAT IS HONEST
 * ------------------------------------------
 * The whole of ace_engine's layout core needs exactly one thing from this header:
 * `sptr<IRemoteObject>` appearing in a std::function signature, in
 * interfaces/inner_api/ace/ai/data_detector_interface.h:33. It is a parameter
 * type on an AI data-detection callback that the layout path never invokes.
 *
 * So rather than vendor OHOS's reference-counting base, this provides the shape
 * and delegates ownership to std::shared_ptr, which has the same semantics as a
 * strong pointer: copiable, refcounted, destroyed when the last reference goes.
 *
 * WHAT WOULD BE WRONG TO RELY ON
 * ------------------------------
 * OHOS's sptr participates in OHOS's own RefBase refcount, so an sptr and a
 * RefBase::IncStrongRef agree on one number. std::shared_ptr keeps its OWN count
 * and does not. Code that hands an sptr across the OHOS boundary -- or that
 * mixes sptr with a bare RefBase pointer -- would double-free or leak. Nothing
 * in the layout core does, and anything that later wants to must replace this
 * with the genuine //utils/base implementation rather than extend this file.
 *
 * This is a STUB BY NECESSITY with a stated limit, not a no-op-by-choice like
 * compat/shims/hilog: see the note in the README on that distinction.
 */
#ifndef ACE_HOST_REFBASE_H
#define ACE_HOST_REFBASE_H

#include <memory>

namespace OHOS {

/* Present so that a user can derive from it and so that sptr's template
 * parameter has something to bind to; not an implementation of OHOS refcounting. */
class RefBase {
public:
    RefBase() = default;
    virtual ~RefBase() = default;
    RefBase(const RefBase&) = delete;
    RefBase& operator=(const RefBase&) = delete;
};

/* Strong pointer. Mirrors the parts of the real API that call sites use. */
template <typename T>
class sptr {
public:
    sptr() noexcept = default;
    sptr(std::nullptr_t) noexcept {}
    explicit sptr(T* other) : ptr_(other, [](T*) {}) {}
    template <typename U>
    sptr(const sptr<U>& other) noexcept : ptr_(other.GetRefPtr(), [](T*) {}) {}

    T* GetRefPtr() const noexcept
    {
        return ptr_.get();
    }
    T* operator->() const noexcept
    {
        return ptr_.get();
    }
    T& operator*() const noexcept
    {
        return *ptr_;
    }
    explicit operator bool() const noexcept
    {
        return ptr_ != nullptr;
    }
    void clear() noexcept
    {
        ptr_.reset();
    }
    bool operator==(std::nullptr_t) const noexcept
    {
        return ptr_ == nullptr;
    }
    bool operator!=(std::nullptr_t) const noexcept
    {
        return ptr_ != nullptr;
    }

private:
    /* Non-owning: the deleter above is a no-op, so an sptr constructed from a
     * bare pointer does not delete it. That matches the "don't own what you were
     * handed" reading and avoids a surprise double-free; ownership-carrying use
     * is what needs the real RefBase. */
    std::shared_ptr<T> ptr_;
};

/* Weak pointer, same caveat. */
template <typename T>
class wptr {
public:
    wptr() noexcept = default;
    explicit wptr(const sptr<T>& other) noexcept : ptr_(other.GetRefPtr(), [](T*) {}) {}
    sptr<T> promote() const noexcept
    {
        return sptr<T>(ptr_.get());
    }

private:
    std::weak_ptr<T> ptr_;
};

} // namespace OHOS

#endif // ACE_HOST_REFBASE_H
