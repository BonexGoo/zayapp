#pragma once
#include <element/boss_image.hpp>

namespace BOSS
{
    class R
    {
    public:
        /// @brief 이름으로 이미지 리소스를 선택
        /// @param name : 이미지 또는 아틀라스 조각의 이름
        /// @param main_only : 메인 런타임의 공용 이미지에서만 찾을지 여부
        R(chars name, bool main_only = false);
        ~R();

    public:
        static void SetAssetPath(id_assetpath assetpath);
        static void SetAtlasDir(chars dirname);
        /// @brief 이미지 핸들과 아틀라스 정보를 초기화
        /// @param withcache : 디스크에 생성한 아틀라스 캐시도 삭제할지 여부
        /// @param keep_main : 메인 런타임의 아틀라스 정보와 디스크 캐시를 유지할지 여부
        static void ClearAll(bool withcache, bool keep_main = false);
        /// @brief 메인 또는 현재 사이트의 아틀라스를 등록
        /// @param key_filename : main/image/ 아래의 공용 아틀라스 키파일명
        /// @param map_filename : 선택한 이미지 디렉터리 기준의 아틀라스 파일명
        /// @param ctx : 이전에 저장한 아틀라스 캐시정보
        /// @param keyver : 아틀라스 키파일의 버전
        /// @param main_atlas : 메인 런타임의 아틀라스인지 여부
        static void AddAtlas(chars key_filename, chars map_filename, const Context& ctx, sint32 keyver = 1, bool main_atlas = false);
        static void SaveAtlas(Context& ctx);
        static bool IsAtlasUpdated();
        static String PrintUpdatedAtlas(bool everything = false);
        static void RebuildAll();
        static void MakeGrapImage(chars name, sint32 x, sint32 y, sint32 w, sint32 h, float blur, bool use_repaint);
        static void ClearImages(Strings names);

    public:
        inline bool exist() const {return mExist;}
        inline sint32 w() const {return (mImage)? mImage->GetWidth() : 0;}
        inline sint32 h() const {return (mImage)? mImage->GetHeight() : 0;}

    public:
        inline operator const Image&() const
        {
            BOSS_ASSERT("이미지가 로드되지 않았습니다", mImage);
            return *mImage;
        }
        inline const Image* operator->() const
        {
            BOSS_ASSERT("이미지가 로드되지 않았습니다", mImage);
            return mImage;
        }

    private:
        Image* mImage;
        bool mExist;
    };
}
