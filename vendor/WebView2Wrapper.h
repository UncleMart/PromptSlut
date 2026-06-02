// WebView2 Wrapper - Minimal wrapper around Microsoft WebView2
// Uses COM and ICoreWebView2 interfaces directly.
// Compatible with MSYS2/MinGW - no Windows SDK WRL dependency.

#ifndef WEBVIEW2_WRAPPER_H
#define WEBVIEW2_WRAPPER_H

#include <windows.h>
#include <string>
#include <functional>

// Forward declarations for WebView2 COM interfaces
extern "C" {
    struct ICoreWebView2Environment;
    struct ICoreWebView2Controller;
    struct ICoreWebView2;
    struct ICoreWebView2NavigationState;
}

// ---------------------------------------------------------------------------
// WebView2 COM Callback base class - no WRL needed
// ---------------------------------------------------------------------------

class WebView2ComBase : public IUnknown
{
public:
    virtual ~WebView2ComBase() {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override
    {
        *ppvObject = this;
        AddRef();
        return S_OK;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override
    {
        return InterlockedIncrement(&ref_count_);
    }

    IFACEMETHODIMP_(ULONG) Release() override
    {
        ULONG new_ref = InterlockedDecrement(&ref_count_);
        if (new_ref == 0)
            delete this;
        return new_ref;
    }

protected:
    WebView2ComBase() : ref_count_(1) {}
    ULONG ref_count_;
};

// ---------------------------------------------------------------------------
// Generic COM callback for any ICoreWebView2*Completed handler
// ---------------------------------------------------------------------------

template<typename T>
class WebView2Callback : public WebView2ComBase
{
public:
    using Handler = std::function<void(T*, HRESULT)>;

    explicit WebView2Callback(Handler handler) : handler_(handler) {}
    virtual ~WebView2Callback() {}

    // Override this in derived classes to declare the correct interface
    virtual HRESULT InvokeImpl(ICoreWebView2Controller* sender, T* args) = 0;

    IFACEMETHODIMP Invoke(ICoreWebView2Controller* sender, T* args, HRESULT* error) override
    {
        handler_(args, *error);
        return S_OK;
    }

private:
    Handler handler_;
};

// Forward declare to allow template instantiation
struct ICoreWebView2EnvironmentCallback;
struct ICoreWebView2CreateCoreWebView2ControllerCompletedHandler;

// ---------------------------------------------------------------------------
// Environment callback wrapper
// ---------------------------------------------------------------------------

class WebView2EnvironmentCallback : public WebView2Callback<ICoreWebView2Environment>
{
public:
    using EnvHandler = std::function<void(ICoreWebView2Environment* env, HRESULT result)>;
    
    explicit WebView2EnvironmentCallback(EnvHandler handler)
        : WebView2Callback<ICoreWebView2Environment>(
            [handler](ICoreWebView2Environment* env, HRESULT result) {
                handler(env, result);
            })
    {}
};

// ---------------------------------------------------------------------------
// Controller callback wrapper
// ---------------------------------------------------------------------------

class WebView2ControllerCallback : public WebView2Callback<ICoreWebView2Controller>
{
public:
    using CtrlHandler = std::function<void(ICoreWebView2Controller* controller, HRESULT result)>;
    
    explicit WebView2ControllerCallback(CtrlHandler handler)
        : WebView2Callback<ICoreWebView2Controller>(
            [handler](ICoreWebView2Controller* ctrl, HRESULT result) {
                handler(ctrl, result);
            })
    {}
};

// ---------------------------------------------------------------------------
// WebView2 Wrapper - Thin wrapper around Microsoft WebView2 runtime
// ---------------------------------------------------------------------------

class WebView2Wrapper
{
public:
    using OnCreated = std::function<void(HWND hwnd, ICoreWebView2Controller* controller)>;
    using OnNavigationComplete = std::function<void(const std::string& url)>;
    using OnDOMContentLoaded = std::function<void()>;

    WebView2Wrapper()
        : hwnd_(nullptr), controller_(nullptr), core_webview_(nullptr), env_(nullptr),
          initialized_(false)
    {
    }

    ~WebView2Wrapper() { Cleanup(); }

    // Initialize WebView2
    bool Initialize(HWND parent, int x, int y, int width, int height, 
                    const OnCreated& on_created = nullptr,
                    const OnNavigationComplete& on_nav_complete = nullptr,
                    const OnDOMContentLoaded& on_dom_complete = nullptr)
    {
        if (initialized_) return true;
        hwnd_ = parent;

        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr) && hr != S_FALSE)
            return false;

        // Create CoreWebView2Environment
        HRESULT hr_env = CreateCoreWebView2Environment(
            nullptr,  // browser path
            nullptr,  // user agent
            nullptr,  // environment options
            NewEnvCallback([this, on_created, on_nav_complete, on_dom_complete](
                ICoreWebView2Environment* env, HRESULT error) -> HRESULT {
                
                if (FAILED(error)) {
                    return error;
                }

                env_ = env;
                env->CreateCoreWebView2Controller(hwnd_, NewCtrlCallback(
                    [this, on_created](ICoreWebView2Controller* ctrl, HRESULT error) -> HRESULT {
                        if (FAILED(error)) {
                            return error;
                        }

                        controller_ = ctrl;
                        
                        // Get the CoreWebView2 interface
                        controller_->get_CoreWebView2(&core_webview_);
                        
                        // Set up navigation event
                        if (core_webview_) {
                            core_webview_->AddNavigationStateChanged(
                                NewNavStateChangedCallback([on_nav_complete](
                                    ICoreWebView2Controller* sender, ICoreWebView2NavigationStateChangedEventArgs* args) -> HRESULT {
                                    
                                    ICoreWebView2* webview;
                                    sender->get_CoreWebView2(&webview);
                                    if (webview) {
                                        ICoreWebView2NavigationState* state;
                                        webview->GetNavigationState(&state);
                                        if (state) {
                                            BOOL is_complete = FALSE;
                                            state->get_IsBackForwardNavigationInProgress(&is_complete);
                                            // Navigation complete when not in progress
                                            state->Release();
                                        }
                                        webview->Release();
                                    }
                                    return S_OK;
                                }));
                        }

                        initialized_ = true;
                        
                        // Resize to parent dimensions
                        SetSize(width, height);
                        
                        if (on_created) {
                            on_created(hwnd_, ctrl);
                        }
                        
                        return S_OK;
                    }));

                return S_OK;
            }));

        return SUCCEEDED(hr_env);
    }

    // Navigate to a URL
    void Navigate(const std::string& url)
    {
        if (core_webview_) {
            core_webview_->Navigate(url.c_str());
        }
    }

    // Execute JavaScript in the WebView
    void ExecuteJavascript(const std::string& javascript)
    {
        if (core_webview_) {
            core_webview_->ExecuteJavascript(javascript.c_str(), nullptr);
        }
    }

    // Inject HTML content as full page
    void SetHtml(const std::string& html)
    {
        if (!core_webview_) return;
        
        // Create a complete HTML document and inject it
        std::string js = "document.open(); document.write(" + EscapeJsString(html) + "); document.close();";
        ExecuteJavascript(js);
    }

    // Set size
    void SetSize(int width, int height)
    {
        if (controller_) {
            controller_->SetBounds({0, 0, static_cast<LONG>(width), static_cast<LONG>(height)});
        }
    }

    // Show/hide
    void Show()
    {
        if (controller_) {
            controller_->Show();
        }
    }

    void Hide()
    {
        if (controller_) {
            controller_->Hide();
        }
    }

    // Get the window handle
    HWND GetHwnd() const { return hwnd_; }

    bool IsInitialized() const { return initialized_; }

private:
    HWND hwnd_;
    ICoreWebView2Controller* controller_;
    ICoreWebView2* core_webview_;
    ICoreWebView2Environment* env_;
    bool initialized_;

    void Cleanup()
    {
        if (controller_)
        {
            controller_->Release();
            controller_ = nullptr;
        }
        if (core_webview_)
        {
            core_webview_->Release();
            core_webview_ = nullptr;
        }
        if (env_)
        {
            env_->Release();
            env_ = nullptr;
        }
        CoUninitialize();
        initialized_ = false;
    }

    // Helper to escape a string for use in JavaScript
    static std::string EscapeJsString(const std::string& s)
    {
        std::string result = "'";
        for (char c : s) {
            switch (c) {
                case '\'': result += "\\'"; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c; break;
            }
        }
        result += "'";
        return result;
    }
};

// ---------------------------------------------------------------------------
// Helper functions to create COM callbacks without WRL
// ---------------------------------------------------------------------------

static WebView2EnvironmentCallback NewEnvCallback(
    std::function<void(ICoreWebView2Environment*, HRESULT)> fn)
{
    return WebView2EnvironmentCallback(fn);
}

static WebView2ControllerCallback NewCtrlCallback(
    std::function<void(ICoreWebView2Controller*, HRESULT)> fn)
{
    return WebView2ControllerCallback(fn);
}

static WebView2Callback<ICoreWebView2NavigationStateChangedEventArgs> NewNavStateChangedCallback(
    std::function<HRESULT(ICoreWebView2Controller*, ICoreWebView2NavigationStateChangedEventArgs*)> fn)
{
    auto* cb = new WebView2Callback<ICoreWebView2NavigationStateChangedEventArgs>(
        [fn](ICoreWebView2NavigationStateChangedEventArgs* args, HRESULT) {
            // This callback gets invoked by the COM framework
            // The actual handler logic is in the caller
        }
    );
    // Note: For full navigation state handling, you'd need a proper dispatch
    // This is a simplified version for basic initialization
    return WebView2Callback<ICoreWebView2NavigationStateChangedEventArgs>(
        [fn](ICoreWebView2NavigationStateChangedEventArgs* args, HRESULT) {
            fn(nullptr, args);  // Simplified - sender would be captured separately
        }
    );
}

// Cast helper for COM interface queries
inline void* QueryInterfaceCast(IUnknown* unknown, REFIID riid)
{
    void* ptr = nullptr;
    unknown->QueryInterface(riid, &ptr);
    return ptr;
}

// Safe cast helper
template<typename T>
static T* SafeCast(IUnknown* unknown)
{
    return static_cast<T*>(QueryInterfaceCast(unknown, __uuidof(T)));
}

#endif // WEBVIEW2_WRAPPER_H
