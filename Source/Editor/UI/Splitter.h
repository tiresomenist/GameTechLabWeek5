#pragma once
#include "Window.h"

class SSplitter :
    public SWindow
{
public:

    virtual ~SSplitter() 
    {
        delete SideLT;
        delete SideRB;
    }

    SWindow* SideLT;    // Left or Top
    SWindow* SideRB;    // Right or Bottom
    
    SWindow* GetLTSide() { return SideLT; }
    SWindow* GetRBSide() { return SideRB; }

    // 스플릿 라인의 영역 rect 얻는 함수
    virtual FRect GetSplitLineRect() const { return FRect{ }; };

    bool IsSplitLineHover(const FPoint& Coord) const
    {
        return GetSplitLineRect().Contains(Coord);
    }

    bool IsDragging() const { return bIsDragging; }

    virtual bool OnMouseDown(const FPoint& Coord) override
    {
        if (MaximizeWindow)
        {
            return false;
        }

        if (IsSplitLineHover(Coord))
        {
            bIsDragging = true;
            bIsLineHovered = true;
            return true;
        }

        if (SideLT && SideLT->OnMouseDown(Coord)) return true;
        if (SideRB && SideRB->OnMouseDown(Coord)) return true;
        return false;
    }

    virtual bool OnMouseUp(const FPoint& Coord) override
    {
        if (bIsDragging)
        {
            bIsDragging = false;
            
            return true;
        }
        if (SideLT) SideLT->OnMouseUp(Coord);
        if (SideRB) SideRB->OnMouseUp(Coord);
        return false;
    }

    

    virtual void Render() override
    {
        if (MaximizeWindow)
        {
            return;
        }

        if (SideLT) SideLT->Render();
        if (SideRB) SideRB->Render();

        FRect Line = GetSplitLineRect();
        ImDrawList* DrawList = ImGui::GetForegroundDrawList();

        ImU32 Color = (bIsDragging || bIsLineHovered) ? IM_COL32(0, 122, 204, 255) : IM_COL32(40, 40, 43, 255);

        DrawList->AddRectFilled(
            ImVec2(Line.X, Line.Y),
            ImVec2(Line.Right(), Line.Bottom()),
            Color
        );
    }

    void SetMaximizeWindow(FViewportClient* InVC)
    {
        MaximizeWindow = InVC;
    }

    float GetSplitRatio() const { return SplitRatio; }
    void SetSplitRatio(float InRatio) { SplitRatio = InRatio; }

protected:
    float SplitRatio = 0.5f;        // LT:RB의 분할 비율
    float SplitterThickness = 8.0f; // 분할된 선의 두께

    bool bIsDragging = false;
    bool bIsLineHovered = false;

    inline static constexpr float MinRatio = 0.05f;
    inline static constexpr float MaxRatio = 0.95f;
};

// 수평배치 (좌우)
class SSplitterH : public SSplitter
{
public:
    virtual void UpdateLayout(const FRect& InRect) override
    {
        if (MaximizeWindow)
        {
            MaximizeWindow->SetRect(InRect.X, InRect.Y, InRect.Width, InRect.Height);
            return;
        }

        Rect = InRect;

        float HalfThick = SplitterThickness * 0.5f;
        float SplitX = InRect.X + (InRect.Width * SplitRatio);

        // 좌측
        FRect LeftRect{ InRect.X, InRect.Y, (SplitX - HalfThick) - InRect.X, InRect.Height };
        FRect RightRect{ SplitX + HalfThick, InRect.Y, InRect.Right() - (SplitX + HalfThick), InRect.Height};

        if (SideLT)
        {
            SideLT->UpdateLayout(LeftRect);
        }

        if (SideRB)
        {
            SideRB->UpdateLayout(RightRect);
        }
    }

    virtual FRect GetSplitLineRect() const override
    {
        float SplitLineX = Rect.X + (Rect.Width * SplitRatio) - (SplitterThickness * 0.5f);
        return FRect{ SplitLineX, Rect.Y, SplitterThickness, Rect.Height };
    }

    virtual bool OnMouseMove(const FPoint& Coord) override
    {
        if (MaximizeWindow)
        {
            return false;
        }

        if (bIsLineHovered || bIsDragging)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        
        bIsLineHovered = IsSplitLineHover(Coord);
        if (bIsDragging)
        {
            float NewRatio = (Coord.X - Rect.X) / Rect.Width;
            SplitRatio = std::clamp(NewRatio, MinRatio, MaxRatio);

            UpdateLayout(Rect);
            return true;
        }

        if (SideLT && SideLT->OnMouseMove(Coord)) return true;
        if (SideRB && SideRB->OnMouseMove(Coord)) return true;
        return false;
    }

    void SetLeft(SWindow* InLeft) { SideLT = InLeft; }
    void SetRight(SWindow* InRight) { SideRB = InRight; }
};

// 수직배치 (상하)
class SSplitterV : public SSplitter
{
public:
    virtual void UpdateLayout(const FRect& InRect) override
    {
        Rect = InRect;

        float HalfThick = SplitterThickness * 0.5f;
        float SplitY = InRect.Y + (InRect.Height * SplitRatio);

        FRect TopRect{ InRect.X, InRect.Y, InRect.Width, (SplitY - HalfThick) - InRect.Y };
        FRect BotRect{ InRect.X, SplitY + HalfThick, InRect.Width, InRect.Bottom() - (SplitY+ HalfThick)};

        if (SideLT)
        {
            SideLT->UpdateLayout(TopRect);
        }

        if (SideRB)
        {
            SideRB->UpdateLayout(BotRect);
        }
    }

    virtual FRect GetSplitLineRect() const override
    {
        float SplitLineY = Rect.Y + (Rect.Height * SplitRatio) - (SplitterThickness * 0.5f);
        return FRect{ Rect.X, SplitLineY, Rect.Width, SplitterThickness };
    }

    virtual bool OnMouseMove(const FPoint& Coord) override
    {
        if (MaximizeWindow)
        {
            return false;
        }

        if (bIsLineHovered || bIsDragging)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }
        
        bIsLineHovered = IsSplitLineHover(Coord);
        if (bIsDragging)
        {
            float NewRatio = (Coord.Y - Rect.Y) / Rect.Height;
            SplitRatio = std::clamp(NewRatio, MinRatio, MaxRatio);

            UpdateLayout(Rect);
            return true;
        }

        if (SideLT && SideLT->OnMouseMove(Coord)) return true;
        if (SideRB && SideRB->OnMouseMove(Coord)) return true;
        return false;
    }

    void SetTop(SWindow* InTop) { SideLT = InTop; }
    void SetBottom(SWindow* InBot) { SideRB = InBot; }

};