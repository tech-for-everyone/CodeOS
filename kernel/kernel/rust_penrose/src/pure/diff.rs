//! A diff of changes to pure State
use crate::pure::{geometry::Rect, screen::Screen};
use crate::prelude::*;
use ::core::hash::Hash;

#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub struct ScreenState<C>
where
    C: Copy + Clone + PartialEq + Eq + Hash,
{
    pub screen: usize,
    pub tag: String,
    pub clients: Vec<C>,
}

impl<C> From<&Screen<C>> for ScreenState<C>
where
    C: Copy + Clone + PartialEq + Eq + Hash,
{
    fn from(s: &Screen<C>) -> Self {
        Self {
            screen: s.index,
            tag: s.workspace.tag.clone(),
            clients: s.workspace.clients().copied().collect(),
        }
    }
}

/// A summary of the information required to update the X server state from
/// our own internal pure [State][0]
///
///   [0]: crate::core::State
#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub struct Snapshot<C>
where
    C: Copy + Clone + PartialEq + Eq + Hash,
{
    /// The focused client if there is one
    pub focused_client: Option<C>,
    /// The state of the focused screen
    pub focused: ScreenState<C>,
    /// The state of other screens
    pub visible: Vec<ScreenState<C>>,
    /// The positions of all visible clients
    pub positions: Vec<(C, Rect)>,
    /// Known clients that are currently not visable on any screen
    pub hidden_clients: Vec<C>,
    /// Clients that have been removed from the pure state since the last snapshot
    pub killed_clients: Vec<C>,
}

impl<C> Snapshot<C>
where
    C: Copy + Clone + PartialEq + Eq + Hash,
{
    pub(crate) fn visible_clients(&self) -> Vec<C> {
        self.positions.iter().map(|(c, _)| *c).collect()
    }

    pub(crate) fn all_clients(&self) -> Vec<C> {
        self.focused
            .clients
            .iter()
            .cloned()
            .chain(self.visible.iter().flat_map(|s| s.clients.iter().cloned()))
            .chain(self.hidden_clients.iter().cloned())
            .collect()
    }
}

/// The internal diff state from the last time that we refreshed pure [State][0].
///
///   [0]: crate::core::State
#[derive(Debug, Default, Clone, PartialEq, Eq)]
pub struct Diff<C>
where
    C: Copy + Clone + PartialEq + Eq + Hash,
{
    /// Previous state
    pub before: Snapshot<C>,
    /// Current state
    pub after: Snapshot<C>,
}

impl<C> Diff<C>
where
    C: Copy + Clone + PartialEq + Eq + Hash,
{
    /// Construct a new diff from a pair of snapshots
    pub fn new(before: Snapshot<C>, after: Snapshot<C>) -> Self {
        Self { before, after }
    }

    /// Update this diff to be between the current `after` snapshot and the new one provided
    pub fn update(&mut self, after: Snapshot<C>) {
        swap(&mut self.before, &mut self.after);
        self.after = after;
    }

    /// The currently focused client (if there is one)
    pub fn focused_client(&self) -> Option<C> {
        self.after.focused_client
    }

    /// Whether or not the focused client changed as part of this diff
    pub fn focused_client_changed(&self) -> bool {
        self.before.focused_client != self.after.focused_client
    }

    /// Whether or not the given client changed its position as part of this diff
    pub fn client_changed_position(&self, id: &C) -> bool {
        let mut it = self.before.positions.iter();
        let before = it.find(|&(c, _)| c == id).map(|(_, r)| *r);
        let mut it = self.after.positions.iter();
        let after = it.find(|&(c, _)| c == id).map(|(_, r)| *r);

        before != after
    }

    /// The ID of the focused screen if it changed as part of this diff
    pub fn newly_focused_screen(&self) -> Option<usize> {
        if self.before.focused.screen != self.after.focused.screen {
            Some(self.after.focused.screen)
        } else {
            None
        }
    }

    /// An iterator of all clients that were added as part of this diff
    pub fn new_clients(&self) -> Vec<C> {
        let before: HashSet<_> = self.before.all_clients().into_iter().collect();
        let mut after = self.after.all_clients();
        after.retain(|c| !before.contains(c));

        after
    }

    /// An iterator of all clients that were hidden as part of this diff
    pub fn hidden_clients(&self) -> Vec<C> {
        let after: HashSet<_> = self.after.visible_clients().into_iter().collect();
        let mut before = self.before.all_clients();
        before.retain(|c| !after.contains(c));

        before
    }

    /// An iterator of all currently visible clients
    pub fn visible_clients(&self) -> Vec<C> {
        self.after.visible_clients()
    }

    /// Clients that were present in the previous snapshot but not the current one
    pub fn withdrawn_clients(&self) -> Vec<C> {
        let after: HashSet<_> = self.after.all_clients().into_iter().collect();
        let mut before = self.before.all_clients();
        before.retain(|c| !after.contains(c));

        before
    }

    /// Clients that have been removed from the pure state since the last snapshot
    pub fn killed_clients(&self) -> Vec<C> {
        self.after.killed_clients.clone()
    }

    /// The set of tags that were visible in the previous snapshot
    pub fn previous_visible_tags(&self) -> HashSet<&str> {
        once(self.before.focused.tag.as_ref())
            .chain(self.before.visible.iter().map(|s| s.tag.as_ref()))
            .collect()
    }

    /// The set of tags that are visible in the current snapshot
    #[allow(dead_code)]
    pub fn current_visible_tags(&self) -> HashSet<&str> {
        once(self.after.focused.tag.as_ref())
            .chain(self.after.visible.iter().map(|s| s.tag.as_ref()))
            .collect()
    }

}


