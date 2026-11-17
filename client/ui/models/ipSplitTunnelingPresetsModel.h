#ifndef IPSPLITTUNNELINGPRESETSMODEL_H
#define IPSPLITTUNNELINGPRESETSMODEL_H

#include <QAbstractListModel>
#include <QStringList>
#include <QVector>

class IpSplitTunnelingPresetsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        PresetIdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        IconPathRole,
        IsAddedRole,
        IsLoadingRole
    };

    explicit IpSplitTunnelingPresetsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void updateModel(const QStringList &presetIds, const QStringList &addedPresetIds,
                     const QStringList &loadingPresetIds);

    static QString presetName(const QString &presetId);

protected:
    QHash<int, QByteArray> roleNames() const override;

private:
    struct Preset
    {
        QString id;
        bool isAdded;
        bool isLoading;
    };

    QVector<Preset> m_presets;
};

#endif // IPSPLITTUNNELINGPRESETSMODEL_H
