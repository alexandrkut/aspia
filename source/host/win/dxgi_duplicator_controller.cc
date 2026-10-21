//
// Aspia Project
// Copyright (C) 2016-2026 Dmitry Chapyshev <dmitry@aspia.ru>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//

#include "host/win/dxgi_duplicator_controller.h"

#include <thread>

#include "base/logging.h"
#include "base/time_types.h"
#include "base/desktop/frame_aligned.h"
#include "host/win/dxgi_frame.h"
#include "host/win/screen_capture_utils.h"

//--------------------------------------------------------------------------------------------------
// static
const char* DxgiDuplicatorController::resultName(DxgiDuplicatorController::Result result)
{
    switch (result)
    {
        case Result::SUCCEEDED:
            return "Succeeded";

        case Result::UNSUPPORTED_SESSION:
            return "Unsupported session";

        case Result::FRAME_PREPARE_FAILED:
            return "Frame preparation failed";

        case Result::INITIALIZATION_FAILED:
            return "Initialization failed";

        case Result::DUPLICATION_FAILED:
            return "Duplication failed";

        case Result::INVALID_MONITOR_ID:
            return "Invalid monitor id";

        default:
            return "Unknown error";
    }
}

//--------------------------------------------------------------------------------------------------
// static
bool DxgiDuplicatorController::isCurrentSessionSupported()
{
    DWORD session_id = 0;

    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session_id))
    {
        LOG(ERROR) << "Failed to retrieve current session Id, current binary may not have "
                      "required priviledge";
        return false;
    }

    return session_id != 0;
}

//--------------------------------------------------------------------------------------------------
DxgiDuplicatorController::DxgiDuplicatorController()
{
    LOG(INFO) << "Ctor";
}

//--------------------------------------------------------------------------------------------------
DxgiDuplicatorController::~DxgiDuplicatorController()
{
    LOG(INFO) << "Dtor";
    deinitialize();
}

//--------------------------------------------------------------------------------------------------
bool DxgiDuplicatorController::isSupported()
{
    return initialize();
}

//--------------------------------------------------------------------------------------------------
DxgiDuplicatorController::Result DxgiDuplicatorController::duplicateMonitor(DxgiFrame* frame, int monitor_id)
{
    DCHECK_GE(monitor_id, 0);
    return doDuplicate(frame, monitor_id);
}

//--------------------------------------------------------------------------------------------------
DxgiDuplicatorController::Result DxgiDuplicatorController::duplicateDesktop(DxgiFrame* frame)
{
    DCHECK(frame);
    return doDuplicateDesktop(frame);
}

//--------------------------------------------------------------------------------------------------
int DxgiDuplicatorController::screenCount()
{
    if (initialize())
        return doScreenCount();

    return 0;
}

//--------------------------------------------------------------------------------------------------
bool DxgiDuplicatorController::deviceNames(QStringList* output)
{
    DCHECK(output);

    if (initialize())
    {
        for (const auto& duplicator : duplicators_)
        {
            for (int i = 0; i < duplicator.screenCount(); ++i)
                output->emplace_back(duplicator.deviceName(i));
        }

        return true;
    }

    return false;
}

//--------------------------------------------------------------------------------------------------
const QRect& DxgiDuplicatorController::desktopRect() const
{
    return desktop_rect_;
}

//--------------------------------------------------------------------------------------------------
const QRect& DxgiDuplicatorController::initialDesktopRect() const
{
    return initial_desktop_rect_;
}

//--------------------------------------------------------------------------------------------------
const QRect& DxgiDuplicatorController::screenRect(int monitor_id) const
{
    DCHECK_GE(monitor_id, 0);

    for (size_t i = 0; i < duplicators_.size(); ++i)
    {
        if (monitor_id >= duplicators_[i].screenCount())
            monitor_id -= duplicators_[i].screenCount();
        else
            return duplicators_[i].screenRect(monitor_id);
    }

    static const QRect kEmptyRect;
    return kEmptyRect;
}

//--------------------------------------------------------------------------------------------------
const QRect& DxgiDuplicatorController::initialScreenRect(int monitor_id) const
{
    DCHECK_GE(monitor_id, 0);

    for (size_t i = 0; i < duplicators_.size(); ++i)
    {
        if (monitor_id >= duplicators_[i].screenCount())
            monitor_id -= duplicators_[i].screenCount();
        else
            return duplicators_[i].initialScreenRect(monitor_id);
    }

    static const QRect kEmptyRect;
    return kEmptyRect;
}

//--------------------------------------------------------------------------------------------------
void DxgiDuplicatorController::requestFullCopy(DxgiFrame* frame)
{
    if (!frame)
        return;

    Context* context = frame->context();
    if (!context)
        return;

    for (size_t i = 0; i < duplicators_.size() && i < context->contexts.size(); ++i)
        duplicators_[i].requestFullCopy(&context->contexts[i]);
}

//--------------------------------------------------------------------------------------------------
DxgiDuplicatorController::Result DxgiDuplicatorController::doDuplicate(DxgiFrame* frame, int monitor_id)
{
    DCHECK(frame);

    // The dxgi components and APIs do not update the screen resolution without a reinitialization.
    // So we use the GetDC() function to retrieve the screen resolution to decide whether dxgi
    // components need to be reinitialized.
    // If the screen resolution changed, it's very likely the next duplicate() function call will
    // fail because of a missing monitor or the frame size is not enough to store the output. So we
    // reinitialize dxgi components in-place to avoid a capture failure.
    // But there is no guarantee GetDC() function returns the same resolution as dxgi APIs, we
    // still rely on dxgi components to return the output frame size.
    // TODO(zijiehe): Confirm whether IDXGIOutput::GetDesc() and IDXGIOutputDuplication::GetDesc()
    // can detect the resolution change without reinitialization.
    QRect rect = ScreenCaptureUtils::fullScreenRect();

    if (!full_screen_rect_.has_value())
    {
        full_screen_rect_ = rect;
    }
    else if (rect != full_screen_rect_)
    {
        full_screen_rect_ = rect;
        deinitialize();
    }

    if (!initialize())
    {
        if (succeeded_duplications_ == 0 && !isCurrentSessionSupported())
        {
            LOG(ERROR) << "Current binary is running in session 0. DXGI components cannot be "
                          "initialized";
            return Result::UNSUPPORTED_SESSION;
        }

        // Cannot initialize COM components now, display mode may be changing.
        return Result::INITIALIZATION_FAILED;
    }

    if (!frame->prepare(selectedDesktopSize(monitor_id), monitor_id))
        return Result::FRAME_PREPARE_FAILED;

    SharedPointer<Frame> shared_frame = frame->frame();

    shared_frame->updatedRegion()->clear();

    setup(frame->context());

    if (ensureFrameCaptured(frame->context(), shared_frame))
    {
        if (doDuplicateOne(frame->context(), monitor_id, shared_frame))
        {
            ++succeeded_duplications_;
            return Result::SUCCEEDED;
        }
    }

    int screen_count = doScreenCount();
    if (monitor_id >= screen_count)
    {
        // It's a user error to provide a |monitor_id| larger than screen count. We do not need to
        // deinitialize.
        LOG(ERROR) << "Invalid monitor id:" << monitor_id << "(screen count=" << screen_count << ")";
        return Result::INVALID_MONITOR_ID;
    }

    // If the |monitor_id| is valid, but doDuplicateAll() or doDuplicateOne failed, something must
    // be wrong from capturer APIs. We should deinitialize().
    deinitialize();

    LOG(ERROR) << "Unable to duplicate frame";
    return Result::DUPLICATION_FAILED;
}

//--------------------------------------------------------------------------------------------------
void DxgiDuplicatorController::unregister(const Context* const context)
{
    if (contextExpired(context))
    {
        // The Context has not been setup after a recent initialization, so it should not been
        // registered in duplicators.
        return;
    }

    for (size_t i = 0; i < duplicators_.size(); ++i)
        duplicators_[i].unregister(&context->contexts[i]);
}

//--------------------------------------------------------------------------------------------------
bool DxgiDuplicatorController::initialize()
{
    if (!duplicators_.empty())
        return true;

    if (doInitialize())
        return true;

    deinitialize();
    return false;
}

//--------------------------------------------------------------------------------------------------
bool DxgiDuplicatorController::doInitialize()
{
    DCHECK(desktop_rect_.isEmpty());
    DCHECK(duplicators_.empty());

    std::vector<D3dDevice> devices = D3dDevice::enumDevices();
    if (devices.empty())
    {
        LOG(ERROR) << "No D3dDevice found";
        return false;
    }

    for (size_t i = 0; i < devices.size(); ++i)
    {
        DxgiAdapterDuplicator duplicator(devices[i]);
        // There may be several video cards on the system, some of them may not support
        // IDXGOutputDuplication. But they should not impact others from taking effect, so we
        // should continually try other adapters. This usually happens when a non-official virtual
        // adapter is installed on the system.
        DxgiAdapterDuplicator::ErrorCode error_code = duplicator.initialize();
        if (error_code != ErrorCode::SUCCESS)
        {
            LOG(ERROR) << "Failed to initialize DxgiAdapterDuplicator on adapter" << i;

            if (error_code == ErrorCode::CRITICAL_ERROR)
            {
                LOG(ERROR) << "Adapter duplicator has critical error. DXGI initialization failed";
                return false;
            }

            continue;
        }

        DCHECK(!duplicator.desktopRect().isEmpty());
        duplicators_.emplace_back(std::move(duplicator));

        desktop_rect_ = desktop_rect_.united(duplicators_.back().desktopRect());
    }

    // The desktop as Windows knows it, before the rects are normalized to the origin. A monitor
    // to the left of the primary one has a negative coordinate there, and the frame of the whole
    // desktop has to start where the desktop starts and not where the screen does.
    initial_desktop_rect_ = QRect();

    for (size_t i = 0; i < duplicators_.size(); ++i)
    {
        for (int j = 0; j < duplicators_[i].screenCount(); ++j)
            initial_desktop_rect_ = initial_desktop_rect_.united(duplicators_[i].initialScreenRect(j));
    }

    translateRect();

    ++identity_;

    if (duplicators_.empty())
    {
        LOG(ERROR) << "Cannot initialize any DxgiAdapterDuplicator instance";
    }

    return !duplicators_.empty();
}

//--------------------------------------------------------------------------------------------------
void DxgiDuplicatorController::deinitialize()
{
    desktop_rect_ = QRect();
    initial_desktop_rect_ = QRect();
    duplicators_.clear();
    full_screen_rect_.reset();
}

//--------------------------------------------------------------------------------------------------
bool DxgiDuplicatorController::contextExpired(const Context* const context) const
{
    DCHECK(context);
    return context->controller_id != identity_ || context->contexts.size() != duplicators_.size();
}

//--------------------------------------------------------------------------------------------------
void DxgiDuplicatorController::setup(Context* context)
{
    if (contextExpired(context))
    {
        DCHECK(context);

        context->contexts.clear();
        context->contexts.resize(duplicators_.size());

        for (size_t i = 0; i < duplicators_.size(); ++i)
            duplicators_[i].setup(&context->contexts[i]);

        context->controller_id = identity_;
    }
}

//--------------------------------------------------------------------------------------------------
DxgiDuplicatorController::Result DxgiDuplicatorController::doDuplicateDesktop(DxgiFrame* frame)
{
    DCHECK(frame);

    // The same change of the screen resolution is watched for as when a single monitor is
    // captured: the dxgi components do not follow it without a reinitialization.
    const QRect rect = ScreenCaptureUtils::fullScreenRect();

    if (!full_screen_rect_.has_value())
    {
        full_screen_rect_ = rect;
    }
    else if (rect != full_screen_rect_)
    {
        full_screen_rect_ = rect;
        deinitialize();
    }

    if (!initialize())
    {
        if (succeeded_duplications_ == 0 && !isCurrentSessionSupported())
        {
            LOG(ERROR) << "Current binary is running in session 0. DXGI components cannot be "
                          "initialized";
            return Result::UNSUPPORTED_SESSION;
        }

        return Result::INITIALIZATION_FAILED;
    }

    // The frame covers the whole desktop, and it is a source of its own: a frame of one monitor
    // and a frame of the whole desktop are not the same thing, and the frame would otherwise
    // keep what it has of the other one.
    if (!frame->prepare(desktopRect().size(), ScreenCapturer::kAllScreensId))
        return Result::FRAME_PREPARE_FAILED;

    SharedPointer<Frame> shared_frame = frame->frame();

    shared_frame->updatedRegion()->clear();

    setup(frame->context());

    if (ensureFrameCaptured(frame->context(), shared_frame))
    {
        if (doDuplicateAll(frame->context(), shared_frame))
        {
            // The frame covers the whole desktop, and the rects of the outputs are normalized to
            // the origin of the desktop, so the frame starts there as well. The offset of the
            // frame is counted from the same corner as the coordinates of the client, and the
            // mouse of the client is placed on the screen by this offset.
            //
            // The rect of the desktop as Windows knows it (initialDesktopRect()) is in the space
            // of the desktop, where a monitor to the left of the primary one has a negative
            // coordinate. That space is the one the cursor of the system is reported in, and it
            // is not the one the frames and the mouse are counted in.
            shared_frame->setTopLeft(desktopRect().topLeft());

            ++succeeded_duplications_;
            return Result::SUCCEEDED;
        }
    }

    deinitialize();

    LOG(ERROR) << "Unable to duplicate the whole desktop";
    return Result::DUPLICATION_FAILED;
}

//--------------------------------------------------------------------------------------------------
bool DxgiDuplicatorController::doDuplicateAll(Context* context, SharedPointer<Frame>& target)
{
    // Every output is drawn in the place it has on the desktop, and the frame is of the size
    // of the desktop. The rects of the outputs are normalized to the origin of the desktop
    // when the controller is initialized (see translateRect()), so the frame and the outputs
    // are counted from the same origin and the output lands in the frame where it belongs.
    for (size_t i = 0; i < duplicators_.size(); ++i)
    {
        if (!duplicators_[i].duplicate(&context->contexts[i], target))
            return false;
    }

    return true;
}

//--------------------------------------------------------------------------------------------------
bool DxgiDuplicatorController::doDuplicateOne(Context* context, int monitor_id, SharedPointer<Frame>& target)
{
    DCHECK(monitor_id >= 0);

    for (size_t i = 0; i < duplicators_.size() && i < context->contexts.size(); ++i)
    {
        if (monitor_id >= duplicators_[i].screenCount())
        {
            monitor_id -= duplicators_[i].screenCount();
        }
        else
        {
            if (duplicators_[i].duplicateMonitor(&context->contexts[i], monitor_id, target))
            {
                target->setTopLeft(duplicators_[i].screenRect(monitor_id).topLeft());
                return true;
            }

            return false;
        }
    }
    return false;
}

//--------------------------------------------------------------------------------------------------
qint64 DxgiDuplicatorController::numFramesCaptured() const
{
    qint64 min = std::numeric_limits<qint64>::max();

    for (const auto& duplicator : duplicators_)
        min = std::min(min, duplicator.numFramesCaptured());

    return min;
}

//--------------------------------------------------------------------------------------------------
int DxgiDuplicatorController::doScreenCount() const
{
    int result = 0;

    for (const auto& duplicator : duplicators_)
        result += duplicator.screenCount();

    return result;
}

//--------------------------------------------------------------------------------------------------
QSize DxgiDuplicatorController::selectedDesktopSize(int monitor_id) const
{
    return screenRect(monitor_id).size();
}

//--------------------------------------------------------------------------------------------------
bool DxgiDuplicatorController::ensureFrameCaptured(Context* context, SharedPointer<Frame>& target)
{
    // On a modern system, the FPS / monitor refresh rate is usually larger than
    // or equal to 60. So 17 milliseconds is enough to capture at least one frame.
    const MilliSeconds ms_per_frame(17);

    // Skips the first frame to ensure a full frame refresh has happened before
    // this function returns.
    const qint64 frames_to_skip = 1;

    // The total time out milliseconds for this function. If we cannot get enough
    // frames during this time interval, this function returns false, and cause
    // the DXGI components to be reinitialized. This usually should not happen
    // unless the system is switching display mode when this function is being
    // called. 500 milliseconds should be enough for ~30 frames.
    const MilliSeconds timeout_ms(500);

    if (numFramesCaptured() >= frames_to_skip)
        return true;

    SharedPointer<Frame> frame;

    if (target->size().width() >= desktopRect().width() &&
        target->size().height() >= desktopRect().height())
    {
        // |target| is large enough to cover entire screen, we do not need to use |fallback_frame|.
        frame = target;
    }
    else
    {
        frame.reset(FrameAligned::create(desktopRect().size(), 32).release());
    }

    const TimePoint start_ms = Clock::now();
    TimePoint last_frame_start_ms;

    while (numFramesCaptured() < frames_to_skip)
    {
        if (numFramesCaptured() > 0)
        {
            // Sleep |ms_per_frame| before capturing next frame to ensure the screen has been
            // updated by the video adapter.
            std::this_thread::sleep_for(ms_per_frame - (Clock::now() - last_frame_start_ms));
        }

        last_frame_start_ms = Clock::now();

        if (!doDuplicateAll(context, frame))
            return false;

        if (Clock::now() - start_ms > timeout_ms)
        {
            LOG(ERROR) << "Failed to capture" << frames_to_skip << "frames within"
                       << timeout_ms.count() << "milliseconds";
            return false;
        }
    }

    // The warm-up above consumed the full-frame request that setup() queued for |context| (it was
    // spent on the throwaway priming frames). Re-arm it so the first frame delivered to the caller is
    // a complete one, not just the diff since the priming frame - otherwise a fresh (black) target
    // buffer shows up black with the picture filling in piece by piece.
    for (size_t i = 0; i < duplicators_.size() && i < context->contexts.size(); ++i)
        duplicators_[i].requestFullCopy(&context->contexts[i]);

    return true;
}

//--------------------------------------------------------------------------------------------------
void DxgiDuplicatorController::translateRect()
{
    const QPoint position = QPoint(0, 0) - desktop_rect_.topLeft();

    desktop_rect_.translate(position);

    for (auto& duplicator : duplicators_)
        duplicator.translateRect(position);
}
