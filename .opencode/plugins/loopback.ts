/**
 * Local OpenCode server plugin.
 *
 * Config (in `.opencode/opencode.json`):
 *   { "plugin": ["./plugins/loopback.ts"] }
 *
 * Exports `server` which OpenCode loads as a plugin module.
 */

let buildInFlight = false
let lastBuildAt = 0

type LoopbackOptions = {
  project?: string
  scheme?: string
  configuration?: string
  minSecondsBetweenBuilds?: number
}

export const id = "loopback"

export const server = async (
  input: { directory: string },
  options: LoopbackOptions = {},
) => {
  const project = options.project ?? "Driver.xcodeproj"
  const scheme = options.scheme ?? "Driver"
  const configuration = options.configuration ?? "Release"
  const minSecondsBetweenBuilds = options.minSecondsBetweenBuilds ?? 60

  const build = async () => {
    if (buildInFlight) return false

    const now = Date.now()
    if (now - lastBuildAt < minSecondsBetweenBuilds * 1000) return false

    buildInFlight = true
    lastBuildAt = now
    try {
      const proc = Bun.spawn(
        [
          "xcodebuild",
          "-project",
          project,
          "-scheme",
          scheme,
          "-configuration",
          configuration,
          "build",
          "CODE_SIGN_IDENTITY=-",
          "CODE_SIGNING_REQUIRED=NO",
        ],
        {
          cwd: input.directory,
          stdout: "inherit",
          stderr: "inherit",
        },
      )
      await proc.exited
      return proc.exitCode === 0
    } finally {
      buildInFlight = false
    }
  }

  return {
    event: async ({ event }: { event: any }) => {
      // Depending on OpenCode version/config, idling can be signaled either as:
      // - `session.idle`
      // - `session.status` with `properties.status.type === "idle"`
      if (event?.type === "session.idle") {
        await build()
        return
      }

      if (event?.type === "session.status") {
        const statusType = event?.properties?.status?.type
        if (statusType === "idle") await build()
      }
    },
  }
}

export default server
