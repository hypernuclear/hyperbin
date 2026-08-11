// Are the flies actually moving, and does crawling only happen on the bin?
#include "FlySim.h"

#include <QRectF>
#include <cmath>
#include <cstdio>

using namespace hyperbin;

int main()
{
    const QRectF bin(500, 500, 40, 28);
    FlySim s(31337);
    s.setBinRect(bin);
    s.setFullness(1.0f);

    double crawlSum = 0, flySum = 0;
    int cn = 0, fn = 0, offBinCrawl = 0;
    for (int i = 0; i < 3000; ++i) {
        s.step(1.0f / 60);
        for (const Fly &f : s.flies()) {
            const double sp = std::hypot(f.vel.x(), f.vel.y());
            const bool over = bin.contains(f.pos);
            if (f.mode == FlyMode::Crawling) {
                crawlSum += sp; ++cn;
                if (!over) ++offBinCrawl;
            } else {
                flySum += sp; ++fn;
            }
        }
    }
    std::printf("crawling: mean %.1f px/s over %d samples; %d of them off the bin\n",
                cn ? crawlSum / cn : 0.0, cn, offBinCrawl);
    std::printf("flying  : mean %.1f px/s over %d samples\n",
                fn ? flySum / fn : 0.0, fn);
    return 0;
}
