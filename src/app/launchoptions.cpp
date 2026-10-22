#include "app/launchoptions.h"

namespace App {

LaunchOptions LaunchOptions::parse(const QStringList &args)
{
    LaunchOptions options;

    for (int i = 0; i < args.size(); ++i) {
        const QString &arg = args[i];
        if ((arg == QLatin1String("-s") || arg == QLatin1String("--session"))
                && i + 1 < args.size()) {
            options.mode = LaunchMode::Session;
            options.sessionId = args[i + 1];
            break;   // 会话模式参数优先，行为与历史版本一致
        } else if (arg == QLatin1String("-t") || arg == QLatin1String("--test")) {
            options.mode = LaunchMode::Test;
        }
    }

    return options;
}

} // namespace App
