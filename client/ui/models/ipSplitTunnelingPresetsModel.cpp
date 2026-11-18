#include "ipSplitTunnelingPresetsModel.h"

#include <algorithm>

namespace
{
    struct PresetView
    {
        QString name;
        QString description;
        QString icon;
        QString services;
    };

    PresetView presetView(const QString &presetId)
    {
        const QHash<QString, PresetView> views = {
            { "youtube", { "YouTube", "YouTube, ReVanced", "youtube",
                "revanced.app, youtube.com" } },
            { "messengers",
              { IpSplitTunnelingPresetsModel::tr("Messengers"), "Telegram, WhatsApp, Signal", "message-circle",
                "messenger.com, signal.org, telegram.org, viber.com, whatsapp.com, zello.com" } },
            { "socials",
              { IpSplitTunnelingPresetsModel::tr("Social networks"), "Instagram, Facebook, LinkedIn", "users",
                "4pda.to, cwer.ws, facebook.com, hornet.com, instagram.com, linkedin.com, patreon.com, quora.com, ru-board.com, snapchat.com, tiktok.com, x.com" } },
            { "ai", { IpSplitTunnelingPresetsModel::tr("AI"), "ChatGPT, Claude, Perplexity", "sparkles",
                "aistudio.google.com, canva.com, chatgpt.com, claude.ai, cursor.com, deepl.com, deepseek.com, elevenlabs.io, grammarly.com, grok.com, kilo.ai, meta.ai, notebooklm.google, perplexity.ai, pollo.ai, stitch.withgoogle.com" } },
            { "discord", { "Discord", IpSplitTunnelingPresetsModel::tr("Voice and text chats"), "headset",
                "discord.com, discord.gg, discord.media" } },
            { "video",
              { IpSplitTunnelingPresetsModel::tr("Movies and series"), "Netflix, HDRezka, LostFilm", "clapperboard",
                "daramalive.life, doramy.club, filmix.fm, hdrezka.ag, kino.watch, kinobase.org, kinovod.pro, kinozal.tv, lostfilm.tv, netflix.com, seasonvar.ru, stremio.com, themoviedb.org, zeflix.online" } },
            { "music", { IpSplitTunnelingPresetsModel::tr("Music"), "Spotify, SoundCloud, Tidal", "music",
                "deezer.com, last.fm, musicbrainz.org, soundcloud.com, spotify.com, tidal.com" } },
            { "games", { IpSplitTunnelingPresetsModel::tr("Games"), "Chess.com, itch.io, Nexus Mods", "gamepad-2",
                "chess.com, heroes.thelazy.net, itch.io, modrinth.com, nexusmods.com" } },
            { "news", { IpSplitTunnelingPresetsModel::tr("News"), "BBC, DW, Euronews", "newspaper",
                "agents.media, agentura.ru, bbc.com, bellingcat.com, cherta.media, colta.ru, currenttime.tv, dept.one, doxa.team, dw.com, echofm.online, ehorussia.com, ej.ru, euronews.com, exler.ru, golosameriki.com, gordonua.com, gulagu.net, holod.media, hrw.org, istories.media, kasparov.ru, kavkaz-uzel.eu, korrespondent.net, krymr.com, meduza.io, memohrc.org, moscowtimes.ru, navalny.com, newtimes.ru, novayagazeta.ru, ovd.info, paperpaper.ru, polit.ru, proekt.media, prostovpn.org, radiosvoboda.org, semnasem.org, svoboda.org, tayga.info, the-village.ru, theins.ru, thetruestory.news, tvrain.ru, unian.net, verstka.media, vot-tak.tv, zona.media" } },
            { "anime", { IpSplitTunnelingPresetsModel::tr("Anime"), "Crunchyroll, MyAnimeList", "tv",
                "amedia.site, anidub.pro, anilibria.tv, animego.org, animevost.org, bato.to, crunchyroll.com, hentailib.me, jut.su, kara.su, kodik.info, mangahub.ru, mangapark.net, myanimelist.net, nyaa.land, nyaa.si, shikimori.one, yummyanime.tv" } },
            { "art", { IpSplitTunnelingPresetsModel::tr("Art"), "DeviantArt, Pixiv, AO3", "palette",
                "archiveofourown.org, danbooru.donmai.us, deviantart.com, e621.net, ficbook.net, gelbooru.com, hentaichan.live, infinitenovel.eu, pixabay.com, pixiv.net, rulate.ru, vndb.org, younettranslate.com" } },
            { "education",
              { IpSplitTunnelingPresetsModel::tr("Education"), "Duolingo, IEEE, Refactoring Guru", "graduation-cap",
                "duolingo.com, flibusta.is, ieee.org, mrakopedia.net, refactoring.guru, wikiart.org, znanija.com" } },
            { "tools", { IpSplitTunnelingPresetsModel::tr("Tools"), "Notion, Miro, Proton", "wrench",
                "10minutemail.com, 9proxy.com, aida64.com, anydesk.com, autodesk.com, basecamp.com, beeble.com, bestchange.ru, capcut.com, clickup.com, connect.garmin.com, fastpic.org, fatsecret.com, fmhy.net, linktr.ee, make.com, medium.com, miro.com, myheritage.com, naukri.com, notepad-plus-plus.org, notion.so, proton.me, proxy6.net, proxyline.net, sankakucomplex.com, sentry.io, strava.com, trainingpeaks.com, tuta.com, whoop.com, wikidot.com, zapier.com" } },
            { "torrent", { IpSplitTunnelingPresetsModel::tr("Torrents"), "NNM-Club, Kinozal, 1337x", "magnet",
                "1337x.to, booktracker.org, filmitorrent.net, freetp.org, kinozal.me, newstudio.tv, nnmclub.to, rustorka.com, rutor.info, rutracker.org, thepiratebay.org, torrent.by" } },
            { "shop", { IpSplitTunnelingPresetsModel::tr("Shops"), "Ubiquiti, Ralph Lauren", "shopping-bag",
                "babook.org, ralphlauren.eu, ui.com" } },
            { "finance", { IpSplitTunnelingPresetsModel::tr("Finance"), "Wise, Aave, Morpho", "wallet",
                "aave.com, euler.finance, morpho.org, wise.com" } },
            { "hosting", { IpSplitTunnelingPresetsModel::tr("Hosting"), "Hetzner, GoDaddy, Namecheap", "server",
                "bitninja.com, cloudlinux.com, godaddy.com, hetzner.com, namecheap.com" } },
            { "jetbrains", { "JetBrains", IpSplitTunnelingPresetsModel::tr("IDE and AI Assistant"), "code-xml",
                "jetbrains.com" } },
            { "casino", { IpSplitTunnelingPresetsModel::tr("Casino"), "Vavada, 7K Casino", "dices",
                "7k.casino, vavada.com" } },
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
    case ServicesRole: return presetView(preset.id).services;
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
    roles[ServicesRole] = "services";
    roles[IconPathRole] = "iconPath";
    roles[IsAddedRole] = "isAdded";
    roles[IsLoadingRole] = "isLoading";
    return roles;
}
