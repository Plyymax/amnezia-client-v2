#ifndef IPSPLITTUNNELINGUICONTROLLER_H
#define IPSPLITTUNNELINGUICONTROLLER_H

#include <QObject>

#include "core/controllers/ipSplitTunnelingController.h"
#include "ui/models/ipSplitTunnelingModel.h"
#include "ui/models/ipSplitTunnelingPresetsModel.h"

class IpSplitTunnelingUiController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int routeMode READ getRouteMode WRITE setRouteMode NOTIFY routeModeChanged)
    Q_PROPERTY(bool isSplitTunnelingEnabled READ isSplitTunnelingEnabled NOTIFY isSplitTunnelingEnabledChanged)
    Q_PROPERTY(IpSplitTunnelingPresetsModel *presetsModel READ presetsModel CONSTANT)

public:
    explicit IpSplitTunnelingUiController(IpSplitTunnelingController* ipSplitTunnelingController,
                                         IpSplitTunnelingModel* ipSplitTunnelingModel, QObject *parent = nullptr);

public slots:
    void addSite(QString hostname);
    void removeSite(int index);
    void removeSites();
    void importSites(const QString &fileName, bool replaceExisting);
    void exportSites(const QString &fileName);
    void toggleSplitTunneling(bool enabled);
    void setRouteMode(int routeMode);
    void addPreset(const QString &presetId);
    void removePreset(const QString &presetId);

    int getRouteMode() const;
    bool isSplitTunnelingEnabled() const;
    IpSplitTunnelingPresetsModel *presetsModel() const;

    void updateModel();

signals:
    void routeModeChanged();
    void isSplitTunnelingEnabledChanged();
    void errorOccurred(const QString &errorMessage);
    void finished(const QString &message);
    void saveFile(const QString &fileName, const QString &data);

private:
    IpSplitTunnelingController* m_ipSplitTunnelingController;
    IpSplitTunnelingModel* m_ipSplitTunnelingModel;
    IpSplitTunnelingPresetsModel *m_presetsModel;

    void updatePresetsModel();
};

#endif // IPSPLITTUNNELINGUICONTROLLER_H
