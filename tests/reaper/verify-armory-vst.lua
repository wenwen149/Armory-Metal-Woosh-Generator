-- Run against the dedicated Armory_VST_Test project with:
-- reaper.exe -nonewinst verify-armory-vst.lua
-- This script never saves the project. It adds one clearly named test track.

local reportPath = os.getenv("TEMP") .. "\\armory-vst-reaper-test.txt"

local function writeReport(message)
  local report = assert(io.open(reportPath, "w"))
  report:write(message .. "\n")
  report:close()
end

local projectName = ({ reaper.EnumProjects(-1, "") })[2] or ""
if not projectName:lower():find("armory_vst_test", 1, true) then
  writeReport("REFUSED: active project is not Armory_VST_Test: " .. projectName)
  return
end

reaper.Undo_BeginBlock()
local trackIndex = reaper.CountTracks(0)
reaper.InsertTrackAtIndex(trackIndex, true)
local track = reaper.GetTrack(0, trackIndex)
reaper.GetSetMediaTrackInfo_String(track, "P_NAME", "__Armory VST verification track__", true)
reaper.SetOnlyTrackSelected(track)

local fxIndex = reaper.TrackFX_AddByName(track, "VST3i: Armory VST", false, 1)
if fxIndex < 0 then
  reaper.DeleteTrack(track)
  reaper.Undo_EndBlock("Armory VST scan check (plugin unavailable)", -1)
  writeReport("NOT_FOUND: REAPER did not expose 'VST3i: Armory VST'. Rescan VST paths, then run again.")
  return
end

local _, fxName = reaper.TrackFX_GetFXName(track, fxIndex, "")
reaper.Undo_EndBlock("Armory VST load verification", -1)
reaper.UpdateArrange()
writeReport("LOADED: " .. fxName .. " on __Armory VST verification track__")
