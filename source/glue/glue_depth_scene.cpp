#include <boss.hpp>
#include "glue_depth_scene.hpp"

ZAY_DECLARE_GLUE_CLASS("depth_scene", GlueDepthScene)

bool GlueDepthScene::Register(ZaySon& NewZayson, const ZayGlue::Bindings& bindings)
{
    const auto GetState = bindings.host.Find<std::shared_ptr<ZayGlueNativeState>()>("native.state");
    const auto FindImage = bindings.host.Find<bool(chars, const Image*&, sint32&, sint32&)>("resource.find_image");
    const auto GetAPng = bindings.host.Find<id_animate(chars)>("animation.get_apng");
    const auto* widget = bindings.widget;
    if(!GetState || !FindImage || !GetAPng || !widget)
    {
        BOSS_TRACE("[depth_scene] Missing required GLUE host callback/widget");
        return false;
    }
    const auto state = (*GetState)();
    if(!state)
    {
        BOSS_TRACE("[depth_scene] Host returned no native state");
        return false;
    }

    NewZayson.AddGlue("createdepthscene", ZAY_DECLARE_GLUE(p, state, widget)
    {
        bool ok = false;
        if(p.ParamCount() == 1)
        {
            const String name = p.Param(0).ToText();
            if(name.Length())
            {
                auto& scene = state->scenes[{widget, (chars) name}];
                if(!scene.handle) scene.handle = Platform::Graphics::CreateDepthScene();
                ok = (scene.handle != nullptr);
            }
        }
        if(p.CanReturn()) p.Return(ok ? "1" : "0");
    }, "[Scene:'map']", "위젯 전용 깊이 장면 생성 또는 재사용");

    NewZayson.AddGlue("removedepthscene", ZAY_DECLARE_GLUE(p, state, widget)
    {
        const bool ok = p.ParamCount() == 1 &&
            state->scenes.erase({widget, (chars) p.Param(0).ToText()}) != 0;
        if(p.CanReturn()) p.Return(ok ? "1" : "0");
    }, "[Scene:'map']", "깊이 장면과 정적 배경 해제");

    NewZayson.AddGlue("depthscenerevision", ZAY_DECLARE_GLUE(p, state)
    {
        if(p.ParamCount() == 0 && p.CanReturn())
            p.Return(String::FromInteger(sint64(state->sceneRevision)));
    }, "", "정적 데이터 또는 렌더링 설정 변경 세대");

    NewZayson.AddGlue("depthscenechanged", ZAY_DECLARE_GLUE(p, state, widget)
    {
        auto* scene = p.ParamCount() == 1 ? state->PaintScene(widget, p.Param(0).ToText()) : nullptr;
        const bool rebuild = !scene || !scene->ready || !scene->built.Same(state->paintView);
        if(p.CanReturn()) p.Return(rebuild ? "1" : "0");
    }, "[Scene:'map']", "현재 뷰포트, 배율 또는 리소스 변경 여부");

    NewZayson.AddGlue("resetdepthscene", ZAY_DECLARE_GLUE(p, state, widget)
    {
        auto* scene = p.ParamCount() == 1 ? state->PaintScene(widget, p.Param(0).ToText()) : nullptr;
        if(scene)
        {
            scene->ready = Platform::Graphics::ResetDepthScene(scene->handle, state->paintView.viewport);
            if(scene->ready) scene->built = state->paintView;
        }
        if(p.CanReturn()) p.Return(scene && scene->ready ? "1" : "0");
    }, "[Scene:'map']", "현재 depthviewport 영역에서 정적 장면 등록 시작");

    NewZayson.AddGlue("adddepthsceneimage", ZAY_DECLARE_GLUE(p, state, FindImage, GetAPng, widget)
    {
        const bool ok = RenderDepthResource(state, FindImage, GetAPng, widget, p, false);
        if(p.CanReturn()) p.Return(ok ? "1" : "0");
    }, "[Scene:'map'][Order:0][Image:r.name][L:0][T:0][R:100][B:100]#[Opacity:1][SrcL:0][SrcT:0][SrcR:100][SrcB:100]", "정적 이미지를 뒤에서 앞으로 등록. 선택 원본영역은 이미지 픽셀 좌표");

    NewZayson.AddGlue("drawdepthscene", ZAY_DECLARE_GLUE(p, state, widget)
    {
        auto* scene = p.ParamCount() == 1 ? state->PaintScene(widget, p.Param(0).ToText()) : nullptr;
        const bool ok = scene && scene->ready && scene->built.Same(state->paintView);
        if(ok) Platform::Graphics::DrawDepthScene(scene->handle);
        if(p.CanReturn()) p.Return(ok ? "1" : "0");
    }, "[Scene:'map']", "뷰포트의 정적 배경을 한 번에 출력");

    NewZayson.AddGlue("drawdepthscenesprite", ZAY_DECLARE_GLUE(p, state, FindImage, GetAPng, widget)
    {
        const bool ok = RenderDepthResource(state, FindImage, GetAPng, widget, p, true);
        if(p.CanReturn()) p.Return(ok ? "1" : "0");
    }, "[Scene:'map'][AfterStatic:-1][Image:r.name][L:0][T:0][R:100][B:100]#[Opacity:1][APngSec:0][SrcL:0][SrcT:0][SrcR:100][SrcB:100]",
        "깊이 장면 위에 캐릭터 출력. 걷기 APNG는 파일명과 재생 초를 전달");
    return true;
}

bool GlueDepthScene::RenderDepthResource(const std::shared_ptr<ZayGlueNativeState>& state,
    const std::shared_ptr<ImageFinder>& FindImage,
    const std::shared_ptr<APngGetter>& GetAPng, const ZayWidget* widget,
    const ZayExtend::Payload& p, bool sprite)
{
    const sint32 count = p.ParamCount();
    const bool hasSource = (count == 12 || count == 13);
    const sint32 prefix = count - (hasSource ? 4 : 0);
    if(prefix < 7 || prefix > (sprite ? 9 : 8)) return false;
    auto* scene = state->PaintScene(widget, p.Param(0).ToText());
    const auto& view = state->paintView;
    if(!scene || !scene->ready || !scene->built.Same(view)) return false;
    // A failed static registration invalidates this build, so a partial background cannot be reused.
    if(!sprite) scene->ready = false;
    const sint32 order = p.Param(1).ToInteger();
    const String name = p.Param(2).ToText();
    double left = p.Param(3).ToFloat(), top = p.Param(4).ToFloat();
    double width = p.Param(5).ToFloat() - left, height = p.Param(6).ToFloat() - top;
    float opacity = count >= 8 ? p.Param(7).ToFloat() : 1.0f;
    if(!std::isfinite(opacity) || !std::isfinite(left) || !std::isfinite(top) ||
        !std::isfinite(width) || !std::isfinite(height) || width < 0 || height < 0) return false;
    if(width == 0 || height == 0)
    {
        if(!sprite) scene->ready = true;
        return true;
    }

    id_image_read image = nullptr;
    if(name.Left(2) == "r." && prefix <= 8)
    {
        const Image* resourceImage = nullptr;
        sint32 resourceWidth = 0, resourceHeight = 0;
        if(!(*FindImage)(name.Offset(2), resourceImage, resourceWidth, resourceHeight) ||
            !resourceImage || !resourceImage->HasBitmap() || resourceWidth <= 0 || resourceHeight <= 0)
            return false;
        if(!hasSource)
        {
            const double kx = width / resourceWidth, ky = height / resourceHeight;
            // Same valid-rectangle origin and integer extent as ZayPanel::stretch(..., UISF_Strong).
            left += std::trunc(-resourceImage->L() * kx);
            top += std::trunc(-resourceImage->T() * ky);
            width = std::trunc(resourceImage->GetImageWidth() * kx);
            height = std::trunc(resourceImage->GetImageHeight() * ky);
            if(width == 0 || height == 0)
            {
                if(!sprite) scene->ready = true;
                return true;
            }
        }
        image = resourceImage->GetBuildImage(view.color);
        opacity *= Math::Min(view.color.a, 128) / 128.0f;
    }
    else if(sprite && prefix == 9 && name.Right(5).CompareNoCase(".apng") == 0)
    {
        const double sec = p.Param(8).ToFloat();
        if(!std::isfinite(sec)) return false;
        if(auto animate = (*GetAPng)(name))
        {
            Platform::Animate::Seek(animate, sec, true);
            image = Platform::Animate::GetImage(animate);
        }
    }
    if(!image) return false;
    const double x = (view.origin.x + left) * view.scale - view.viewport.l;
    const double y = (view.origin.y + top) * view.scale - view.viewport.t;
    const rect128f dst = {float(x), float(y), float(x + width * view.scale), float(y + height * view.scale)};
    rect128f src = {0, 0, float(Platform::Graphics::GetImageWidth(image)), float(Platform::Graphics::GetImageHeight(image))};
    if(hasSource)
        src = {float(p.Param(prefix).ToFloat()), float(p.Param(prefix + 1).ToFloat()),
            float(p.Param(prefix + 2).ToFloat()), float(p.Param(prefix + 3).ToFloat())};
    if(!std::isfinite(src.l) || !std::isfinite(src.t) || !std::isfinite(src.r) || !std::isfinite(src.b) ||
        !std::isfinite(dst.l) || !std::isfinite(dst.t) || !std::isfinite(dst.r) || !std::isfinite(dst.b) ||
        !(src.r > src.l && src.b > src.t && dst.r > dst.l && dst.b > dst.t)) return false;
    if(sprite) Platform::Graphics::DrawDepthSceneSprite(scene->handle, order, image, src, dst, opacity);
    else scene->ready = Platform::Graphics::AddDepthSceneImage(scene->handle, order, image, src, dst, opacity);
    return scene->ready;
}
