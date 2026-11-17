#include "ipSplitTunnelingPresetsModel.h"

#include <algorithm>

namespace
{
    struct PresetView
    {
        QString name;
        QString description;
        QString icon;
    };

    PresetView presetView(const QString &presetId)
    {
        const QHash<QString, PresetView> views = {
            { "youtube", { "YouTube", "YouTube, ReVanced", "youtube" } },
            { "messengers",
              { IpSplitTunnelingPresetsModel::tr("Messengers"), "Telegram, WhatsApp, Signal", "message-circle" } },
            { "socials",
              { IpSplitTunnelingPresetsModel::tr("Social networks"), "Instagram, Facebook, LinkedIn", "users" } },
            { "ai", { IpSplitTunnelingPresetsModel::tr("AI"), "ChatGPT, Claude, Perplexity", "sparkles" } },
            { "discord", { "Discord", IpSplitTunnelingPresetsModel::tr("Voice and text chats"), "headset" } },
            { "video",
              { IpSplitTunnelingPresetsModel::tr("Movies and series"), "Netflix, HDRezka, LostFilm", "clapperboard" } },
            { "music", { IpSplitTunnelingPresetsModel::tr("Music"), "Spotify, SoundCloud, Tidal", "music" } },
            { "games", { IpSplitTunnelingPresetsModel::tr("Games"), "Chess.com, itch.io, Nexus Mods", "gamepad-2" } },
            { "news", { IpSplitTunnelingPresetsModel::tr("News"), "BBC, DW, Euronews", "newspaper" } },
            { "anime", { IpSplitTunnelingPresetsModel::tr("Anime"), "Crunchyroll, MyAnimeList", "tv" } },
            { "art", { IpSplitTunnelingPresetsModel::tr("Art"), "DeviantArt, Pixiv, AO3", "palette" } },
            { "education",
              { IpSplitTunnelingPresetsModel::tr("Education"), "Duolingo, IEEE, Refactoring Guru", "graduation-cap" } },
            { "tools", { IpSplitTunnelingPresetsModel::tr("Tools"), "Notion, Miro, Proton", "wrench" } },
            { "torrent", { IpSplitTunnelingPresetsModel::tr("Torrents"), "NNM-Club, Kinozal, 1337x", "magnet" } },
            { "shop", { IpSplitTunnelingPresetsModel::tr("Shops"), "Ubiquiti, Ralph Lauren", "shopping-bag" } },
            { "finance", { IpSplitTunnelingPresetsModel::tr("Finance"), "Wise, Aave, Morpho", "wallet" } },
            { "hosting", { IpSplitTunnelingPresetsModel::tr("Hosting"), "Hetzner, GoDaddy, Namecheap", "server" } },
            { "jetbrains", { "JetBrains", IpSplitTunnelingPresetsModel::tr("IDE and AI Assistant"), "code-xml" } },
            { "casino", { IpSplitTunnelingPresetsModel::tr("Casino"), "Vavada, 7K Casino", "dices" } },
            { "ru-sites",
              { IpSplitTunnelingPresetsModel::tr("Russian sites"),
                IpSplitTunnelingPresetsModel::tr("Banks, Gosuslugi, stores"), "globe-2" } },
            { "ru-services",
              { IpSplitTunnelingPresetsModel::tr("Russian services"),
                IpSplitTunnelingPresetsModel::tr("IP networks of banks and stores"), "landmark" } },
            { "ru-networks",
              { IpSplitTunnelingPresetsModel::tr("All of Runet"),
                IpSplitTunnelingPresetsModel::tr("Every Russian IP network"), "map" } },
        };
        return views.value(presetId);
    }
}

IpSplitTunnelingPresetsModel::IpSplitTunnelingPresetsModel(QObject *parent) : QAbstractListModel(parent)
{
}

int IpSplitTunnelingPresetsModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_presets.size();
}

QVariant IpSplitTunnelingPresetsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_presets.size()) {
        return QVariant();
    }

    const Preset &preset = m_presets.at(index.row());
    switch (role) {
    case PresetIdRole: return preset.id;
    case NameRole: return presetView(preset.id).name;
    case DescriptionRole: return presetView(preset.id).description;
    case IconPathRole: return QString("qrc:/images/controls/%1.svg").arg(presetView(preset.id).icon);
    case IsAddedRole: return preset.isAdded;
    case IsLoadingRole: return preset.isLoading;
    default: return QVariant();
    }
}

void IpSplitTunnelingPresetsModel::updateModel(const QStringList &presetIds, const QStringList &addedPresetIds,
                                               const QStringList &loadingPresetIds)
{
    QVector<Preset> presets;
    for (const QString &presetId : presetIds) {
        presets.append({ presetId, addedPresetIds.contains(presetId), loadingPresetIds.contains(presetId) });
    }

    const bool sameIds = std::equal(presets.cbegin(), presets.cend(), m_presets.cbegin(), m_presets.cend(),
                                    [](const Preset &left, const Preset &right) { return left.id == right.id; });
    if (!sameIds) {
        beginResetModel();
        m_presets = presets;
        endResetModel();
        return;
    }

    m_presets = presets;
    if (!m_presets.isEmpty()) {
        emit dataChanged(index(0), index(m_presets.size() - 1), { IsAddedRole, IsLoadingRole });
    }
}

QString IpSplitTunnelingPresetsModel::presetName(const QString &presetId)
{
    return presetView(presetId).name;
}

QHash<int, QByteArray> IpSplitTunnelingPresetsModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[PresetIdRole] = "presetId";
    roles[NameRole] = "name";
    roles[DescriptionRole] = "description";
    roles[IconPathRole] = "iconPath";
    roles[IsAddedRole] = "isAdded";
    roles[IsLoadingRole] = "isLoading";
    return roles;
}
