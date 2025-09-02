if not crankboy then
    print("no crankboy struct")
    return
end

if playdate.system and playdate.system.abortDeviceLock then
    crankboy.setHasSystemPrivileges()
end

function playdate.deviceWillLock()
    if playdate.system and playdate.system.abortDeviceLock and crankboy.onSystemDeviceLock() then
        playdate.system.abortDeviceLock()
    end
end