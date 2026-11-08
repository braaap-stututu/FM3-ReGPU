# Run with: qrenderdoc.exe --python rd_list.py
# Lists every draw/dispatch/copy of one capture: event id, name, viewport, scissor, output targets (with sizes).
import os
import renderdoc as rd

CAPTURE = os.environ.get("RD_CAPTURE", "")
OUT = os.environ.get("RD_OUT", "")

def main():
    lines = []
    cap = rd.OpenCaptureFile()
    result = cap.OpenFile(CAPTURE, "", None)
    if result != rd.ResultCode.Succeeded:
        lines.append("open failed: %s" % str(result))
        return lines
    result, controller = cap.OpenCapture(rd.ReplayOptions(), None)
    if result != rd.ResultCode.Succeeded:
        lines.append("replay failed: %s" % str(result))
        return lines
    textures = {t.resourceId: t for t in controller.GetTextures()}
    names = {}
    for r in controller.GetResources():
        names[r.resourceId] = r.name

    def walk(actions):
        for a in actions:
            if a.children:
                walk(a.children)
            flags = a.flags
            if not (flags & (rd.ActionFlags.Drawcall | rd.ActionFlags.Dispatch | rd.ActionFlags.Copy | rd.ActionFlags.Resolve | rd.ActionFlags.Clear)):
                continue
            controller.SetFrameEvent(a.eventId, False)
            pipe = controller.GetPipelineState()
            desc = ""
            if flags & rd.ActionFlags.Drawcall:
                try:
                    vp = pipe.GetViewport(0)
                    sc = pipe.GetScissor(0)
                    desc += " vp %dx%d@%d,%d sc %d,%d-%d,%d" % (vp.width, vp.height, vp.x, vp.y, sc.x, sc.y, sc.x + sc.width, sc.y + sc.height)
                except Exception as e:
                    desc += " vp ?"
                outs = []
                try:
                    for o in pipe.GetOutputTargets():
                        if o.resource != rd.ResourceId.Null():
                            t = textures.get(o.resource)
                            outs.append("%s(%s %dx%d)" % (names.get(o.resource, str(o.resource)), str(t.format.Name()) if t else "?", t.width if t else 0, t.height if t else 0))
                    d = pipe.GetDepthTarget()
                    if d.resource != rd.ResourceId.Null():
                        t = textures.get(d.resource)
                        outs.append("depth:%s(%dx%d)" % (names.get(d.resource, str(d.resource)), t.width if t else 0, t.height if t else 0))
                except Exception as e:
                    outs.append("?")
                desc += " out " + ",".join(outs)
            lines.append("%6d %s%s" % (a.eventId, a.GetName(controller.GetStructuredFile()), desc))

    walk(controller.GetRootActions())
    controller.Shutdown()
    cap.Shutdown()
    return lines

try:
    out = main()
except Exception as e:
    import traceback
    out = ["exception: " + traceback.format_exc()]
with open(OUT, "w") as f:
    f.write("\n".join(out) + "\n")
os._exit(0)
