# Run with: qrenderdoc.exe --python rd_save.py
# For each event in RD_EVENTS (comma list), saves the bound colour target(s) and depth target as PNG
# (depth: the depth channel and, separately, the stencil channel) into RD_OUTDIR.
import os
import renderdoc as rd

CAPTURE = os.environ["RD_CAPTURE"]
OUTDIR = os.environ["RD_OUTDIR"]
EVENTS = [int(e) for e in os.environ["RD_EVENTS"].split(",")]

def main():
    log = []
    cap = rd.OpenCaptureFile()
    if cap.OpenFile(CAPTURE, "", None) != rd.ResultCode.Succeeded:
        return ["open failed"]
    result, controller = cap.OpenCapture(rd.ReplayOptions(), None)
    if result != rd.ResultCode.Succeeded:
        return ["replay failed"]
    os.makedirs(OUTDIR, exist_ok=True)
    for eid in EVENTS:
        controller.SetFrameEvent(eid, True)
        pipe = controller.GetPipelineState()
        targets = [("c%d" % i, o.resource) for i, o in enumerate(pipe.GetOutputTargets()) if o.resource != rd.ResourceId.Null()]
        d = pipe.GetDepthTarget()
        if d.resource != rd.ResourceId.Null():
            targets.append(("depth", d.resource))
            targets.append(("stencil", d.resource))
        for tag, res in targets:
            s = rd.TextureSave()
            s.resourceId = res
            s.destType = rd.FileType.PNG
            s.mip = 0
            s.slice.sliceIndex = 0
            s.alpha = rd.AlphaMapping.Discard
            if tag == "depth":
                s.channelExtract = 0
                s.comp.blackPoint = 0.0
                s.comp.whitePoint = 1.0
            elif tag == "stencil":
                s.channelExtract = 1
                s.comp.blackPoint = 0.0
                s.comp.whitePoint = 255.0
            path = os.path.join(OUTDIR, "e%d_%s.png" % (eid, tag))
            r = controller.SaveTexture(s, path)
            log.append("event %d %s -> %s (%s)" % (eid, tag, path, str(r)))
    controller.Shutdown()
    cap.Shutdown()
    return log

try:
    out = main()
except Exception:
    import traceback
    out = ["exception: " + traceback.format_exc()]
with open(os.path.join(OUTDIR, "save_log.txt"), "w") as f:
    f.write("\n".join(out) + "\n")
os._exit(0)
