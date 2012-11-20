#ifndef buffer_hh_INCLUDED
#define buffer_hh_INCLUDED

#include <vector>
#include <list>
#include <memory>

#include "line_and_column.hh"
#include "option_manager.hh"
#include "hook_manager.hh"
#include "string.hh"
#include "units.hh"

namespace Kakoune
{

class Buffer;
class Window;

struct BufferCoord : LineAndColumn<BufferCoord, LineCount, ByteCount>
{
    constexpr BufferCoord(LineCount line = 0, ByteCount column = 0)
        : LineAndColumn(line, column) {}
};

// A BufferIterator permits to iterate over the characters of a buffer
class BufferIterator
{
public:
    typedef char value_type;
    typedef size_t difference_type;
    typedef const value_type* pointer;
    typedef const value_type& reference;
    typedef std::bidirectional_iterator_tag iterator_category;

    BufferIterator() : m_buffer(nullptr) {}
    BufferIterator(const Buffer& buffer, BufferCoord coord);

    bool operator== (const BufferIterator& iterator) const;
    bool operator!= (const BufferIterator& iterator) const;
    bool operator<  (const BufferIterator& iterator) const;
    bool operator<= (const BufferIterator& iterator) const;
    bool operator>  (const BufferIterator& iterator) const;
    bool operator>= (const BufferIterator& iterator) const;

    char   operator* () const;
    size_t operator- (const BufferIterator& iterator) const;

    BufferIterator operator+ (ByteCount size) const;
    BufferIterator operator- (ByteCount size) const;

    BufferIterator& operator+= (ByteCount size);
    BufferIterator& operator-= (ByteCount size);

    BufferIterator& operator++ ();
    BufferIterator& operator-- ();

    BufferIterator operator++ (int);
    BufferIterator operator-- (int);

    void clamp(bool avoid_eol);

    bool is_begin() const;
    bool is_end() const;
    bool is_valid() const;

    void on_insert(const BufferCoord& begin, const BufferCoord& end);
    void on_erase(const BufferCoord& begin, const BufferCoord& end);

    const Buffer& buffer() const;
    const BufferCoord& coord() const { return m_coord; }
    LineCount  line() const { return m_coord.line; }
    ByteCount column() const { return m_coord.column; }

private:
    ByteCount offset() const;

    safe_ptr<const Buffer> m_buffer;
    BufferCoord   m_coord;
    friend class Buffer;
};

class BufferChangeListener
{
public:
    virtual void on_insert(const BufferIterator& begin, const BufferIterator& end) = 0;
    virtual void on_erase(const BufferIterator& begin, const BufferIterator& end) = 0;
};

// A Buffer is a in-memory representation of a file
//
// The Buffer class permits to read and mutate this file
// representation. It also manage modifications undo/redo and
// provides tools to deal with the line/column nature of text.
class Buffer : public SafeCountable
{
public:
    enum class Flags
    {
        None = 0,
        File = 1,
        New  = 2,
        Fifo = 4
    };

    Buffer(String name, Flags flags, String initial_content = "\n");
    Buffer(const Buffer&) = delete;
    Buffer(Buffer&&) = delete;
    Buffer& operator= (const Buffer&) = delete;
    ~Buffer();

    Flags flags() const { return m_flags; }
    Flags& flags() { return m_flags; }

    void insert(BufferIterator pos, String content);
    void erase(BufferIterator begin, BufferIterator end);

    size_t         timestamp() const { return m_timestamp; }

    void           begin_undo_group();
    void           end_undo_group();
    bool           undo();
    bool           redo();
    void           reset_undo_data();

    String         string(const BufferIterator& begin,
                          const BufferIterator& end) const;

    BufferIterator begin() const;
    BufferIterator end() const;
    ByteCount      character_count() const;
    LineCount      line_count() const;
    ByteCount      line_length(LineCount line) const;

    // returns an iterator at given coordinates. line_and_column is
    // clamped according to avoid_eol.
    BufferIterator iterator_at(const BufferCoord& line_and_column,
                               bool avoid_eol = false) const;
    BufferCoord    line_and_column_at(const BufferIterator& iterator) const;

    // returns nearest valid coordinates from given ones
    // if avoid_eol, clamp to character before eol if line is not empty
    BufferCoord    clamp(const BufferCoord& line_and_column,
                         bool avoid_eol = false) const;

    const String& name() const { return m_name; }

    // Window handling
    using WindowList = std::vector<std::unique_ptr<Window>>;
    const WindowList& windows() const { return m_windows; }
    Window& new_window();
    void    delete_window(Window& window);

    // returns true if the buffer is in a different state than
    // the last time it was saved
    bool is_modified() const;

    // notify the buffer that it was saved in the current state
    void notify_saved();

    void add_change_listener(BufferChangeListener& listener) const;
    void remove_change_listener(BufferChangeListener& listener) const;

    // returns an iterator pointing to the first character of the line
    // iterator is on
    BufferIterator iterator_at_line_begin(const BufferIterator& iterator) const;
    // the same but taking a line number instead of an iterator
    BufferIterator iterator_at_line_begin(LineCount line) const;

    // returns an iterator pointing to the character after the last of the
    // line iterator is on (which is the first of the next line if iterator is
    // not on the last one)
    BufferIterator iterator_at_line_end(const BufferIterator& iterator) const;
    // the same but taking a line number instead of an iterator
    BufferIterator iterator_at_line_end(LineCount line) const;

    const String& line_content(LineCount line) const
    { return m_lines[line].content; }

    OptionManager&       option_manager()       { return m_option_manager; }
    const OptionManager& option_manager() const { return m_option_manager; }
    HookManager&         hook_manager()         { return m_hook_manager; }
    const HookManager&   hook_manager()   const { return m_hook_manager; }

private:
    friend class BufferIterator;

    void check_invariant() const;

    struct Line
    {
        ByteCount start;
        String    content;

        ByteCount length() const { return content.length(); }
    };
    struct LineList : std::vector<Line>
    {
    public:
        Line& operator[](LineCount line)
        { return std::vector<Line>::operator[]((int)line); }

        const Line& operator[](LineCount line) const
        { return std::vector<Line>::operator[]((int)line); }
    };
    LineList m_lines;

    void do_insert(const BufferIterator& pos, const String& content);
    void do_erase(const BufferIterator& begin, const BufferIterator& end);

    String  m_name;
    Flags   m_flags;

    struct Modification;
    typedef std::vector<Modification> UndoGroup;

    std::vector<UndoGroup>           m_history;
    std::vector<UndoGroup>::iterator m_history_cursor;
    UndoGroup                        m_current_undo_group;

    void apply_modification(const Modification& modification);
    void revert_modification(const Modification& modification);

    WindowList m_windows;

    size_t m_last_save_undo_index;
    size_t m_timestamp;

    // this mutable as adding or removing listeners is not muting the buffer
    // observable state.
    mutable std::vector<BufferChangeListener*> m_change_listeners;

    OptionManager m_option_manager;
    HookManager   m_hook_manager;
};

constexpr Buffer::Flags operator|(Buffer::Flags lhs, Buffer::Flags rhs)
{
    return (Buffer::Flags)((int) lhs | (int) rhs);
}

inline Buffer::Flags& operator|=(Buffer::Flags& lhs, Buffer::Flags rhs)
{
    (int&) lhs |= (int) rhs;
    return lhs;
}

constexpr bool operator&(Buffer::Flags lhs, Buffer::Flags rhs)
{
    return ((int) lhs & (int) rhs) != 0;
}

inline Buffer::Flags& operator&=(Buffer::Flags& lhs, Buffer::Flags rhs)
{
    (int&) lhs &= (int) rhs;
    return lhs;
}

constexpr Buffer::Flags operator~(Buffer::Flags lhs)
{
    return (Buffer::Flags)(~(int)lhs);
}


}

#include "buffer_iterator.inl.hh"

#endif // buffer_hh_INCLUDED
