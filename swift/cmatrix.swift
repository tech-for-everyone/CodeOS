// CodeOS cmatrix - The Matrix Digital Rain Terminal Utility
// Analogous Swift Implementation for CodeOS CLI Utilities

#if canImport(Glibc)
import Glibc
#elseif canImport(Darwin)
import Darwin
#endif

import Foundation

// ANSI Terminal Codes
let ANSI_CLEAR_SCREEN = "\u{001B}[2J\u{001B}[H"
let ANSI_HIDE_CURSOR  = "\u{001B}[?25l"
let ANSI_SHOW_CURSOR  = "\u{001B}[?25h"
let ANSI_RESET        = "\u{001B}[0m"
let ANSI_BOLD         = "\u{001B}[1m"
let ANSI_DIM          = "\u{001B}[2m"

enum MatrixColor: String, CaseIterable {
    case green, red, blue, cyan, magenta, yellow, white, rainbow

    var darkCode: String {
        switch self {
        case .green:   return "\u{001B}[32m"
        case .red:     return "\u{001B}[31m"
        case .blue:    return "\u{001B}[34m"
        case .cyan:    return "\u{001B}[36m"
        case .magenta: return "\u{001B}[35m"
        case .yellow:  return "\u{001B}[33m"
        case .white:   return "\u{001B}[37m"
        case .rainbow: return "\u{001B}[32m"
        }
    }

    var brightCode: String {
        switch self {
        case .green:   return "\u{001B}[92m"
        case .red:     return "\u{001B}[91m"
        case .blue:    return "\u{001B}[94m"
        case .cyan:    return "\u{001B}[96m"
        case .magenta: return "\u{001B}[95m"
        case .yellow:  return "\u{001B}[93m"
        case .white:   return "\u{001B}[97m"
        case .rainbow: return "\u{001B}[92m"
        }
    }
}

// Matrix Character Pools
let katakanaChars: [Character] = (0xFF66...0xFF9D).compactMap { UnicodeScalar($0) }.map { Character($0) }
let asciiChars: [Character] = (33...126).compactMap { UnicodeScalar($0) }.map { Character($0) }
let digitChars: [Character] = Array("0123456789")
let symbolChars: [Character] = Array("!@#$%^&*()_+-=[]{}|;:,.<>?/~`0123456789ABCDEF")

struct TerminalSize {
    var cols: Int
    var rows: Int

    static func current() -> TerminalSize {
        var w = winsize()
        if ioctl(STDOUT_FILENO, UInt(TIOCGWINSZ), &w) == 0 && w.ws_col > 0 && w.ws_row > 0 {
            return TerminalSize(cols: Int(w.ws_col), rows: Int(w.ws_row))
        }
        return TerminalSize(cols: 80, rows: 24)
    }
}

class RainStream {
    var col: Int
    var maxRows: Int
    var charPool: [Character]
    var length: Int = 12
    var headY: Int = 0
    var stepDelay: Int = 1
    var stepCounter: Int = 0
    var symbols: [Character] = []
    var rainbowColor: MatrixColor = .green

    init(col: Int, maxRows: Int, charPool: [Character], initial: Bool = true) {
        self.col = col
        self.maxRows = maxRows
        self.charPool = charPool
        reset(initial: initial)
    }

    func reset(initial: Bool = false) {
        length = Int.random(in: 6...max(8, maxRows - 3))
        if initial {
            headY = Int.random(in: -maxRows...maxRows)
        } else {
            headY = Int.random(in: (-length - 5)...(-1))
        }
        stepDelay = [1, 1, 2, 2, 3].randomElement() ?? 1
        stepCounter = 0
        symbols = (0..<length).map { _ in charPool.randomElement() ?? "0" }
        rainbowColor = MatrixColor.allCases.filter { $0 != .rainbow }.randomElement() ?? .green
    }

    func advance() {
        stepCounter += 1
        if stepCounter >= stepDelay {
            stepCounter = 0
            headY += 1

            // Randomly mutate characters inside the stream
            if !symbols.isEmpty && Double.random(in: 0...1) < 0.25 {
                let idx = Int.random(in: 0..<symbols.count)
                symbols[idx] = charPool.randomElement() ?? "1"
            }

            if headY - length > maxRows {
                reset()
            }
        }
    }
}

class CMatrixSwiftApp {
    var color: MatrixColor
    var bold: Bool
    var allBold: Bool
    var updateDelayUs: UInt32
    var screensaver: Bool
    var useKatakana: Bool
    var isRainbow: Bool
    var paused: Bool = false
    var running: Bool = true

    var cols: Int = 80
    var rows: Int = 24
    var streams: [RainStream] = []
    var charPool: [Character]

    var origTermios = termios()
    var rawModeActive = false

    init(color: MatrixColor = .green,
         bold: Bool = true,
         allBold: Bool = false,
         updateDelayMs: Double = 40.0,
         screensaver: Bool = false,
         useKatakana: Bool = true,
         isRainbow: Bool = false) {
        self.color = color
        self.bold = bold
        self.allBold = allBold
        self.updateDelayUs = UInt32(max(5.0, updateDelayMs) * 1000.0)
        self.screensaver = screensaver
        self.useKatakana = useKatakana
        self.isRainbow = isRainbow || (color == .rainbow)

        if useKatakana {
            self.charPool = katakanaChars + asciiChars
        } else {
            self.charPool = symbolChars + digitChars
        }

        updateDimensions()
        initStreams()
    }

    func updateDimensions() {
        let size = TerminalSize.current()
        self.cols = max(10, size.cols)
        self.rows = max(5, size.rows)
    }

    func initStreams() {
        streams.removeAll()
        for c in 0..<cols {
            streams.append(RainStream(col: c, maxRows: rows, charPool: charPool, initial: true))
            if Double.random(in: 0...1) < 0.4 {
                streams.append(RainStream(col: c, maxRows: rows, charPool: charPool, initial: true))
            }
        }
    }

    func enableRawMode() {
        if isatty(STDIN_FILENO) != 0 {
            tcgetattr(STDIN_FILENO, &origTermios)
            var raw = origTermios
            raw.c_lflag &= ~tcflag_t(ECHO | ICANON)
            tcsetattr(STDIN_FILENO, TCSANOW, &raw)
            rawModeActive = true
        }
    }

    func disableRawMode() {
        if rawModeActive {
            tcsetattr(STDIN_FILENO, TCSANOW, &origTermios)
            rawModeActive = false
        }
        print("\(ANSI_SHOW_CURSOR)\(ANSI_RESET)\(ANSI_CLEAR_SCREEN)", terminator: "")
        fflush(stdout)
    }

    func checkInput() {
        var tv = timeval(tv_sec: 0, tv_usec: 0)
        var fds = fd_set()
        
        // Zero and set STDIN_FILENO
        #if canImport(Darwin)
        let fd = STDIN_FILENO
        let mask = Int32(1 << (fd % 32))
        fds.fds_bits.0 = mask
        #else
        var __fds_bits: [Int] = Array(repeating: 0, count: 16)
        __fds_bits[0] = 1 << STDIN_FILENO
        #endif

        var buf: UInt8 = 0
        if read(STDIN_FILENO, &buf, 1) > 0 {
            if screensaver {
                running = false
                return
            }
            let ch = Character(UnicodeScalar(buf))
            if ch == "q" || ch == "Q" || buf == 3 { // 'q' or Ctrl+C
                running = false
            } else if ch == " " {
                paused = !paused
            } else if ch == "b" {
                bold = !bold
            } else if ch == "B" {
                allBold = !allBold
            } else if ch == "r" {
                isRainbow = !isRainbow
            } else if ch == "c" {
                let all = MatrixColor.allCases.filter { $0 != .rainbow }
                if let idx = all.firstIndex(of: color) {
                    color = all[(idx + 1) % all.count]
                }
            } else if ch.isNumber, let val = Int(String(ch)) {
                updateDelayUs = UInt32((10.0 + Double(val) * 15.0) * 1000.0)
            }
        }
    }

    func renderFrame() {
        struct Cell {
            var ch: Character
            var style: String
        }

        var grid: [Int: Cell] = [:] // key: r * cols + c

        let baseDark = color.darkCode
        let baseBright = color.brightCode

        for stream in streams {
            if !paused {
                stream.advance()
            }

            var streamDark = baseDark
            var streamBright = baseBright
            if isRainbow {
                streamDark = stream.rainbowColor.darkCode
                streamBright = stream.rainbowColor.brightCode
            }

            for i in 0..<stream.length {
                let y = stream.headY - i
                if y >= 0 && y < rows {
                    let ch = stream.symbols[i % stream.symbols.count]
                    var style: String
                    if i == 0 {
                        style = "\(ANSI_BOLD)\u{001B}[97m" // bright white head
                    } else if i < 3 {
                        style = (bold || allBold ? ANSI_BOLD : "") + streamBright
                    } else {
                        style = (allBold ? ANSI_BOLD : ANSI_DIM) + streamDark
                    }
                    grid[y * cols + stream.col] = Cell(ch: ch, style: style)
                }
            }
        }

        var buffer = "\u{001B}[H"
        buffer.reserveCapacity(cols * rows * 12)

        for r in 0..<rows {
            for c in 0..<cols {
                if let cell = grid[r * cols + c] {
                    buffer.append(cell.style)
                    buffer.append(cell.ch)
                    buffer.append(ANSI_RESET)
                } else {
                    buffer.append(" ")
                }
            }
            if r < rows - 1 {
                buffer.append("\n")
            }
        }

        print(buffer, terminator: "")
        fflush(stdout)
    }

    func run() {
        enableRawMode()
        print("\(ANSI_HIDE_CURSOR)\(ANSI_CLEAR_SCREEN)", terminator: "")
        fflush(stdout)

        while running {
            checkInput()
            if !running { break }
            renderFrame()
            usleep(updateDelayUs)
        }

        disableRawMode()
    }
}

// Main Command-Line Driver
func printUsage() {
    print("""
    CodeOS cmatrix 2.0 (Swift Edition)
    Usage: cmatrix-swift [options]

    Options:
      -b, --bold          Enable bold leading characters (default: on)
      -B, --all-bold      Make all characters bold
      -C, --color <name>  Color (green, red, blue, cyan, magenta, yellow, white, rainbow)
      -u, --update <ms>   Update delay in ms (default: 40)
      -s, --screensaver   Screensaver mode (exit on any keypress)
      -m, --ascii-only    Use ASCII symbols only (no katakana)
      -V, --version       Show version
      -h, --help          Show help
    """)
}

func main() {
    var color: MatrixColor = .green
    var bold = true
    var allBold = false
    var delayMs = 40.0
    var screensaver = false
    var useKatakana = true
    var isRainbow = false

    let args = CommandLine.arguments
    var i = 1
    while i < args.count {
        let arg = args[i]
        switch arg {
        case "-h", "--help":
            printUsage()
            return
        case "-V", "--version":
            print("CodeOS cmatrix 2.0 (Swift Edition)")
            return
        case "-b", "--bold":
            bold = true
        case "-B", "--all-bold":
            allBold = true
        case "-s", "--screensaver":
            screensaver = true
        case "-m", "--ascii-only":
            useKatakana = false
        case "-C", "--color":
            if i + 1 < args.count {
                i += 1
                let cStr = args[i].lowercased()
                if let matched = MatrixColor(rawValue: cStr) {
                    color = matched
                    if matched == .rainbow { isRainbow = true }
                }
            }
        case "-u", "--update":
            if i + 1 < args.count {
                i += 1
                if let d = Double(args[i]) {
                    delayMs = d
                }
            }
        default:
            break
        }
        i += 1
    }

    let app = CMatrixSwiftApp(
        color: color,
        bold: bold,
        allBold: allBold,
        updateDelayMs: delayMs,
        screensaver: screensaver,
        useKatakana: useKatakana,
        isRainbow: isRainbow
    )
    app.run()
}

main()

