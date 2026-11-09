#pragma once

#include "Priority.h"

#include <objidl.h>
#include <gdiplus.h>

#include <vector>

struct OpenApp {
    AppTarget target;
    std::wstring title;
    std::unique_ptr<Gdiplus::Bitmap> icon;
};

void RefreshOpenApps(std::vector<OpenApp>& apps);
void LoadAppIcon(OpenApp& app);
