from thirdpartyupdater import ThirdPartyUpdater
_updater = ThirdPartyUpdater('/opt/victronenergy/mqtt-rpc/thirdparty/pylon/py-tool', listArgs='-l', updateArgs='-u', updateTimeout=400)

def device_list():
	return _updater.device_list()

def device_update(filename, connection_id, feedbacksender):
	return _updater.device_update(filename, connection_id, feedbacksender)
